#include <kernel/fs/vfs.h>
#include <kernel/process.h>

int sys_open(const char *pathname, int flags, ...)
{
	extern struct process *process_current;

	// Get current process
	if (process_current == NULL)
		return (-1);

	// Try to find free file slot
	int fd = -1;
	while (++fd < PROCESS_NB_OPEN_FILE &&
			process_current->opfile[fd].status == PROCESS_FILE_SLOT_USED);
	if (fd >= PROCESS_NB_OPEN_FILE)
		return (-1);

	// TODO: handle flags and mode !!
	(void)flags;

	// Try to find inode
	if (vfs_open(&process_current->opfile[fd].file, pathname, flags) != 0)
		return (-1);

	// Return the file descriptor
	// @note:
	// * fd = 0 -> STDOUT_FILENO
	// * fd = 1 -> STDERR_FILENO
	// * fd = 2 -> STDIN_FILENO
	return (fd + 3);
}
