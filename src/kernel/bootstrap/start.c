#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>
#include <kernel/context.h>
#include <kernel/atomic.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <kernel/util.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/stat.h>
#include <kernel/util.h>

//TODO: remove me !
#include <kernel/fs/smemfs.h>
#include <kernel/fs/gladfs.h>
#include <kernel/loader.h>
#include <kernel/devices/tty.h>

// Internal symbols
mpu_t current_mpu = MPU_UNKNOWN;

// Internal hardware context.
fx9860_context_t casio_context;
fx9860_context_t vhex_context;

// Symbols defined by the linker
extern uint32_t bbss;
extern uint32_t sbss;
extern uint32_t bdata_ram;
extern uint32_t bdata_rom;
extern uint32_t sdata;
extern uint32_t bvhex_ram;
extern uint32_t bvhex_rom;
extern uint32_t svhex;
extern uint32_t bubc_ram;
extern uint32_t bubc_rom;
extern uint32_t subc;
extern uint32_t brom;
extern uint32_t srom;
extern uint32_t bctors;
extern uint32_t ectors;
extern uint32_t bdtors;
extern uint32_t edtors;

// Internal functions.
extern void vhex_context_set(void);
extern void kernel_switch(common_context_t *context);
extern mpu_t mpu_get(void);
extern int main(void);

// Internal object
extern struct file_system_type gladfs_filesystem;
extern struct file_system_type smemfs_filesystem;


//
// rom_explore() - explore all add-in ROM part.
// @note:
//	For now, we can not use the MMU so we can
// not handle TLB miss. Casio load only the first 4ko
// of the add-in. To avoid TLB miss we explore all
// add-in binary to force Casio to generate all pages
// into the TLB.
//
static void rom_explore(volatile void *rom, int32_t size)
{
	uint8_t unused;

	(void)unused;
	while (size >= 0)
	{
		unused = *(volatile uint8_t*)rom;
		rom = (void*)((uint32_t)rom + 1024);
		size = size - 1024;
	}
}

/* section_execute() - Used to execute contructors and destructors */
static void section_execute(void *bsection, void *esection)
{
	while ((uint32_t)bsection < (uint32_t)esection)
	{
		((void (*)(void))*((uint32_t*)bsection))();
		bsection = (void*)((uint32_t)bsection + 4);
	}
}


/* start() - Kernel entry point */
__attribute__((section(".pretext")))
int start(void)
{
	extern uint32_t vram[256];
	int error;

	//--
	//	Bootstrap part !
	//--

	// Wipe .bss section and dump .data / Vhex sections
	memset(&bbss, 0x00, (size_t)&sbss);
	memcpy(&bdata_ram, &bdata_rom, (size_t)&sdata);

	// Check MPU hardware.
	current_mpu = mpu_get();
	if (current_mpu != MPU_SH7305)
	{
		return (0);
	}

	// Execute constructor.
	section_execute(&bctors, &ectors);

	// Load UBC / VBR space.
	memcpy(&bubc_ram, &bubc_rom, (size_t)&subc);
	memcpy(&bvhex_ram, &bvhex_rom, (size_t)&svhex);

	// Casio do not load all add-in
	// page. So we need to "force" update TLB
	// before switching the VBR.
	rom_explore(&brom, (int32_t)&srom);

	// Save Casio's hardware context and set
	// Vhex hardware context.
	// @note:
	// 	This operation should be atomic
	// because if an interruption or exception
	// occur during the hardware context change
	// the calculator will crash.
	// And this is why between each `atomic_start`
	// and `atomic_end()` the code *SHOULD* be
	// exception safe.
	atomic_start();
	fx9860_context_save(&casio_context);
	vhex_context_set();
	atomic_stop();

	//---
	//	File System part !
	//---

	// Internal FS init !
	gladfs_initialize();
	smemfs_initialize();

	// Initilize Virtual File System
	vfs_register_filesystem(&gladfs_filesystem);
	vfs_register_filesystem(&smemfs_filesystem);

	// Creat initial file tree
	vfs_mount(NULL, NULL, "gladfs", VFS_MOUNT_ROOT, NULL);
	vfs_mkdir("/dev", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	vfs_mkdir("/home", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	vfs_mkdir("/mnt", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	vfs_mkdir("/mnt/smemfs", S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	vfs_mount(NULL, "/mnt/smemfs", "smemfs", /*MS_RDONLY*/0, NULL);
	
	// Add devices
	vfs_mknod("/dev/tty", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
			dev_make_major(TTY_DEV_MAJOR));


	// Test mode !
	//extern void kernel_test(void);
	//kernel_test();

	//---
	//	Start first process !
	//---

	// Create first process: Vhex.
	struct process *vhex_process = process_create("Vhex");
	if (vhex_process == NULL)
	{
		kvram_clear();
		kvram_print(0, 0, "Vhex fatal error !");
		kvram_print(0, 1, "First process error !");
		kvram_print(0, 2, "Wait manual reset...");
		kvram_display();
		while (1) { __asm__ volatile ("sleep"); }
	}

	// Load programe.
	//vhex_process->context.spc = (uint32_t)&kernel_test;
	vhex_process->context.spc = (uint32_t)loader("/mnt/smemfs/VHEX/shell.elf", vhex_process);
	if (vhex_process->context.spc == 0x00000000)
	{
		// Display message.
		kvram_clear();
		kvram_print(0, 0, "Vhex fatal error !");
		kvram_print(0, 1, "File \"VHEX/shell.elf\" not found !");
		kvram_print(0, 2, "Press [MENU key]...");
		kvram_display();

		// Restore Casio context.
		fx9860_context_restore(&casio_context);
		atomic_stop();

		// Casio VRAM workaround
		// @note: GetKey will call Bdisp_PutDD_VRAM(),
		// so save the current internal Video RAM data into
		// Casio's VRAM.
		memcpy(casio_Bdisp_GetVRAM(), vram, 1024);

		// Wait MENU key.
		unsigned int key;
		while (1)
		{
			casio_GetKey(&key);
		}
	}

	// DEBUG !
	kvram_clear();
	printk(0, 0, "Initialize scheduler !");
	printk(0, 1, "Try to start sceduler...");
	kvram_display();
	DBG_WAIT;

	// Initialize sheduler !!
	sched_initialize();
	sched_add_task(vhex_process);
	sched_start();

	// normally the kernel SHOULD / CAN not arrive here.
	kvram_clear();
	kvram_print(0, 0, "Kernel job fini !");
	kvram_display();
	while (1)
	{
		__asm__ volatile ("sleep");
	}
}
