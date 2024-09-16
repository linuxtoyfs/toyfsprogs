// SPDX-License-Identifier: GPL-2.0-only

#ifndef __TOYFS_TYPES_H
#define __TOYFS_TYPES_H

#include <linux/fs.h>

/* We only support 2048 block size */
#define TFS_BSIZE	2048

/* Yup. Filesystem maximum size is 1MiB */
#define TFS_MAX_BLKS	512

/*
 * Number of blocks allocated for a single inode
 * Yes, to make things simple, we hardcode it.
 *
 * Each block address is encoded within a 32-bit integer,
 * the whole tfs_dinode structure is 36 bytes + 4 * num of blocks
 * We use a amaximum of 7 blocks here so the whole inode structure
 * is rounded to a power of 2 (64 bytes).
 *
 * So, in the single inode block, we can have 32 inodes
 * Yes, the whole filesystem will have at most 32 inodes
 */
#define TFS_MAX_INODES	32

/* Every inode can use up to 7 data blocks*/
#define	TFS_MAX_INO_BLKS	7

#define TFS_INVALID	0xdeadbeef

/*
 * directory entry name is hardcoded within the dir entry, set a maximum value
 * Set it to 28 bytes, so the whole dir entry struct is rounded to a power of 2 (32bytes)
 */
#define TFS_MAX_NLEN 28

#define TFS_MAGIC 0x5F544F59 /* _TOY */

/* Inode alloc flags */
#define TFS_INODE_INUSE	1
#define TFS_INODE_FREE	0

/* s_flags fields */
#define TFS_SB_CLEAN	0
#define TFS_SB_DIRTY	1

/* Disk location of metadata blocks */
#define TFS_SB_BLOCK		(0)
#define TFS_INODE_BLOCK		(1)
#define TFS_BITMAP_BLOCK	(2)
#define TFS_FIRST_DATA_BLOCK	(3)
#define TFS_LAST_DATA_BLOCK	(TFS_MAX_BLKS -1)

/* On disk superblock */
struct tfs_dsb {
	__u32	s_magic;
	__u32	s_flags;

	/* free inode and block fields require locking */
	__u32	s_nifree;
	__u32	s_nbfree;
	__u32	s_inodes[32];
};

struct tfs_fs_info {
	int		  bdev_fd;	/* Block dev fd */
	void		  *bmap;	/* bmap buffer */
	struct tfs_dinode *i_arr;	/* inode array */
	struct tfs_dsb	  *sb_buf;	/* On-disk SB buffer */
};

/* On disk inode */
struct tfs_dinode {
	__u32	i_mode;
	__u32	i_nlink;
	__u32	i_atime;
	__u32	i_mtime;
	__u32	i_ctime;
	__u32	i_uid;
	__u32	i_gid;
	__u32	i_size;
	__u32	i_blocks;
	__u32	i_addr[TFS_MAX_INO_BLKS];
};

/* On disk directory entry */
struct tfs_dentry {
	__u32	d_ino;
	char	d_name[TFS_MAX_NLEN];
};

#endif /* __TOYFS_TYPES_H */
