#include <kernel/fs/gladfs.h>
#include <kernel/fs/stat.h>
#include <kernel/atomic.h>

void *gladfs_creat(void *parent_inode, const char *file_name, mode_t mode)
{
	extern struct gladfs_superblock_s gladfs_superblock;
	struct gladfs_inode_s *new_inode;
	struct gladfs_inode_s *parent;

	// Start atomic operation
	atomic_start();

	// Create new inode
	new_inode = gladfs_superblock.super_op.alloc_inode(file_name, mode | __S_IFREG);
	if (new_inode == NULL)
		return (NULL);

	// Update FHS
	parent = parent_inode;
	new_inode->parent = parent_inode;
	new_inode->next = parent->children;
	parent->children = new_inode;

	// Stp atomic operation
	atomic_end();

	// Return inode
	return(new_inode);
}
