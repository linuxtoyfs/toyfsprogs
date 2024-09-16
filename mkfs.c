// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "toyfs_types.h"

/* Block device FD */
int	dfd = 0;

int init_sb(struct tfs_fs_info *tfi)
{
	void *buf = tfi->sb_buf;
	struct tfs_dsb dsb = {};
	int i;

	memset(buf, 0, TFS_BSIZE);

	dsb.s_magic = TFS_MAGIC;
	dsb.s_flags = TFS_SB_CLEAN;
	dsb.s_nifree = 32; /* ino 0 for root dir, ino 1 for dummy file */
	dsb.s_nbfree = 512; /* rootdir, sb, ino block, bmap block, dummy file */

	for (i = 0; i < TFS_MAX_INODES; i++)
		dsb.s_inodes[i] = TFS_INODE_FREE;

	memcpy(tfi->sb_buf, &dsb, sizeof(struct tfs_dsb));

	return 0;
}

int init_inode_array(struct tfs_fs_info *tfi)
{
	int i = 0;
	int j = 0;
	struct tfs_dinode *inode_array = tfi->i_arr;

	/*
	 * We could set the whole inode block to zero, but let's initialize each inode manually so
	 * we walk through the whole inode array
	 */
	for (i = 0; i < TFS_MAX_INODES; i ++) {
		inode_array[i].i_mode = 0;
		inode_array[i].i_nlink = 0;
		inode_array[i].i_atime = 0;
		inode_array[i].i_ctime = 0;
		inode_array[i].i_uid = 0;
		inode_array[i].i_gid = 0;
		inode_array[i].i_size = 0;
		inode_array[i].i_blocks = 0;

		/* Init each addr */
		for (j = 0; j < TFS_MAX_INO_BLKS; j++) {
			inode_array[i].i_addr[j] = TFS_INVALID;
		}
	}
	return 0;
}

/*
 * XXX: This ended up being more complicated than I wanted,
 * I should probably rewrite it.
 *
 * We only have 512 blocks on the whole filesystem, and for
 * that we require 64 bytes on the bitmap.
 * Everything within the bitmap from byte 65 and up should be empty.
 */
void init_bitmap(struct tfs_fs_info *tfi)
{
	memset(tfi->bmap, 0, TFS_BSIZE);
}

void init_inode(struct tfs_dinode* dip, __u32 mode)
{
	time_t tm = 0;

	time(&tm);
	dip->i_mode = mode;
	dip->i_uid = 0;
	dip->i_gid = 0;
	dip->i_atime = (__u32)tm;
	dip->i_mtime = (__u32)tm;
	dip->i_ctime = (__u32)tm;
}

int __alloc_inode(struct tfs_fs_info *tfi, int inum)
{
	struct tfs_dsb	*dsb = tfi->sb_buf;

	if (dsb->s_inodes[inum] != TFS_INODE_FREE) {
		printf("Inode %d already allocated\n", inum);
		return -1;
	}

	dsb->s_inodes[inum] = TFS_INODE_INUSE;
	dsb->s_nifree--;
	return inum;
}

/*
 * Reserve an inode in the inode array
 * Return: inode number (idx in the array)
 */
int get_free_inode(struct tfs_fs_info *tfi)
{
	struct tfs_dsb		*dsb;
	int			i = 0;

	dsb = tfi->sb_buf;

	if (!tfi->i_arr) {
		printf("shit went wrong on dip\n");
		return -1;
	}
	if (!dsb) {
		printf("shit went wrong on dip\n");
		return -1;
	}

	for (i = 0; i < TFS_MAX_INODES; i++) {
		if (dsb->s_inodes[i] == TFS_INODE_FREE)
			break;
		printf("Inode %d in use\n", i);
	}

	/* No free inode? */
	if (i >= TFS_MAX_INODES)
		return -1;

	printf("Found inode %d\n", i);
	return __alloc_inode(tfi, i);
}


int __alloc_block(struct tfs_fs_info *tfi, unsigned int block)
{
	int group = block / 8;
	int bit = block % 8;
	char *bmap = tfi->bmap;

	if (bmap[group] & (1UL << bit)) {
	    printf("Block already in use\n");
	    return -1;
	}

	bmap[group] |= (1UL << bit);
	tfi->sb_buf->s_nbfree--;
	return block;
}

/*
 * XXX: I need to think about a better way to do this.
 * We don't really have block groups here, but we have
 * 512 bits to walk through and check if the block is
 * free or not, we can't do this in a single 512 bits
 * array (I think), so, split the whole bitmap in 8-bit
 * groups, to make things simpler for now.
 */
int get_free_block(struct tfs_fs_info *tfi)
{
	char *bgroups = tfi->bmap;
	int i = 0, j = 0;
	int block = 0;

	for (i = 0; i < (TFS_MAX_BLKS / 8); i++) {
		/* Current group has free blocks? */
		if (bgroups[i] != 0xFF)
			break;
	}

	if (i >= TFS_MAX_BLKS)
		return -1;

	while(1) {
		if (!(bgroups[i] & (1 << j)))
			break;
		j++;
	}
	block = i * 8 + j;
	return __alloc_block(tfi, block);
}

int write_block(struct tfs_fs_info *tfi, void *buf, int block)
{
	int err = 0;
	unsigned int offset = block * TFS_BSIZE;

	err = lseek(tfi->bdev_fd, offset, SEEK_SET);
	if (err != offset)
		return -1;
	err = write(tfi->bdev_fd, buf, TFS_BSIZE);
	if (err != TFS_BSIZE)
		return -1;

	return block;
}

int flush_metadata(struct tfs_fs_info *tfi)
{
	int err = 0;

	/* write bitmap block */
	err = write_block(tfi, tfi->bmap, TFS_BITMAP_BLOCK);
	if (err != TFS_BITMAP_BLOCK) {
		fprintf(stderr, "Failed to write bitmap block\n");
		goto err_out;
	}

	/* write inodes block */
	/* we must write exactly 32 inodes here of 64 bytes */
	err = write_block(tfi, tfi->i_arr, TFS_INODE_BLOCK);
	if (err != TFS_INODE_BLOCK) {
		fprintf(stderr, "Failed to write inodes to disk.\n");
		goto err_out;
	}

	/* write super block */
	err = write_block(tfi, tfi->sb_buf, TFS_SB_BLOCK);
	if (err != TFS_SB_BLOCK) {
		fprintf(stderr, "Failed to write superblock to disk.\n");
		goto err_out;
	}
	return 0;

err_out:
	fprintf(stderr, "Metadata might have been partially written, FS CORRUPTED!!!!!\n");
	return -1;
}

/* Initialize the root directory and add a dummy file for testing */
int init_rootfs(struct tfs_fs_info *tfi)
{
	int error = 0, inum = -1, i = 0;
	int dummy_fd;
	char *dbuf = NULL;
	struct tfs_dinode *root_ino;
	struct tfs_dinode *dummy_ino; /* dummy file */
	struct tfs_dentry *dentries;

	dentries = malloc((TFS_BSIZE / sizeof(struct tfs_dentry)) *
			  sizeof(struct tfs_dentry));

	if (!dentries)
		goto err_out;

	/* Init all dentries to an invalid ref */

	for (i = 0; i < (TFS_BSIZE / sizeof(struct tfs_dentry)); i++) {
		dentries[i].d_ino = TFS_INVALID;
	}

	inum = get_free_inode(tfi);
	if (inum < 0) {
		printf("Failed to allocate inode for rootfs\n");
		error = inum;
		goto err_ino_out;
	}

	printf("Allocated inode %d for rootfs\n", inum);
	root_ino = &tfi->i_arr[inum];

	/* Get another inode for the dummy file */
	inum = get_free_inode(tfi);
	if (inum < 0) {
		printf("Failed to allocate inode for dummy file\n");
		error = inum;
		goto err_ino_out;
	}
	dummy_ino = &tfi->i_arr[inum];
	printf("Allocated inode %d for dummy file\n", inum);

	init_inode(root_ino, S_IFDIR | 0755);
	root_ino->i_nlink = 3; /* "." ".." and dummy file added later here */
	root_ino->i_size = 3 * sizeof(struct tfs_dentry); /* ., .. and dummy file */
	root_ino->i_blocks = 1;

	error = get_free_block(tfi);
	if (error < 0) {
		fprintf(stderr, "Unable to get a free block for root ino\n");
		goto err_ino_out;
	} else {
		root_ino->i_addr[0] = error;
	}

	/* Init directory entries */
	dentries[0].d_ino = 0;
	strcpy(dentries[0].d_name, ".");
	dentries[1].d_ino = 0;
	strcpy(dentries[1].d_name, "..");

	/*
	 * Add a single small file for testing purposes.
	 * This should be wrapped on a mkfs option not as default
	 */
	dummy_fd = open("./sunshine.txt", O_RDONLY);

	if (dummy_fd < 0) {
		printf("Unable to open dummy_fd\n");
		error = -1;
		goto err_ino_out;
	}

	dbuf = malloc(TFS_BSIZE);
	if (!dbuf) {
		fprintf(stderr, "Unable to allocate memory for dummy_fd\n");
		error = -1;
		goto err_ino_out;
	}

	memset(dbuf, 0, TFS_BSIZE);

	/* FIXME: dummy file size shouldn't be hardcoded */

	error = read(dummy_fd, dbuf, 778);
	if (error < 0) {
		fprintf(stderr, "Unable to read dummy file content\n");
		goto err_rd_dummy;
	}

	init_inode(dummy_ino, S_IFREG | 0755);
	dummy_ino->i_nlink = 1; /*just rootfs points to it */
	dummy_ino->i_size = 778;
	dummy_ino->i_blocks = 1;

	error = get_free_block(tfi);
	if (error < 0) {
		fprintf(stderr, "Unable to get a free block for dummy ino\n");
		goto err_write;
	} else {
		dummy_ino->i_addr[0] = error;
	}

	/* setup root dir dentry for dummy fd */
	dentries[2].d_ino = 1;
	strcpy(dentries[2].d_name, "sunshine.txt");

	printf("writing dummy file\n");
	if (write_block(tfi, dbuf, dummy_ino->i_addr[0]) != dummy_ino->i_addr[0]) {
		fprintf(stderr, "dummy ino data miswritten\n");
		error = -1;
		goto err_write;
	}

	printf("Writing rootfs dentries\n");
	if (write_block(tfi, dentries, root_ino->i_addr[0]) != root_ino->i_addr[0]) {
		fprintf(stderr, "rootfs dentries miswritten\n");
		error = -1;
		goto err_write;
	}

	error = flush_metadata(tfi);
	if (error) {
		fprintf(stderr, "Unable to flush metadata\n");
		error = -1;
		goto err_write;
	}
	error = 0;

err_write:
	close(dummy_fd);
err_rd_dummy:
	free(dbuf);
err_ino_out:
	free(dentries);
err_out:
	return error;
}

int main(int argc, char **argv)
{
	struct tfs_fs_info *tfi;
	int error = 0;

	if (argc != 2) {
		fprintf(stderr, "Please specify block device\n");
		goto err_out;
	}

	tfi = malloc(sizeof(struct tfs_fs_info));
	if (!tfi)
		goto err_out;

	tfi->bdev_fd = open(argv[1], O_WRONLY);
	if (tfi->bdev_fd < 0) {
		fprintf(stderr, "Unable to open block device: %s\n", argv[1]);
		goto err_bdev_open;
	}

	/* Even though the SB is small, we always write the whole block */
	tfi->sb_buf = malloc(TFS_BSIZE);
	if (!tfi->sb_buf) {
		printf("Couldn't allocate sb buffer\n");
		goto err_sb_buf;
	}

	tfi->i_arr = malloc(TFS_MAX_INODES * sizeof(struct tfs_dinode));
	if (!tfi->i_arr) {
		fprintf(stderr, "inode array allocation failed\n");
		goto err_i_arr;
	}

	tfi->bmap = malloc(TFS_BSIZE);
	if (!tfi->bmap) {
		fprintf(stderr, "Couldn't allocate bitmap buffer\n");
		goto err_bmap;
	}

	error = init_sb(tfi);
	if (error) {
		fprintf(stderr, "Error initializing superblock\n");
		goto err_bmap;
	}

	error = init_inode_array(tfi);
	if (error) {
		fprintf(stderr, "Error initializing inode_array\n");
		goto err_bmap;
	}

	init_bitmap(tfi);

	/*
	 * Every metadata structure is already initialized,
	 * reserve their blocks on bitmap
	 */
	if (__alloc_block(tfi, TFS_SB_BLOCK) != TFS_SB_BLOCK)
		goto err_bmap;
	if (__alloc_block(tfi, TFS_INODE_BLOCK) != TFS_INODE_BLOCK)
		goto err_bmap;
	if (__alloc_block(tfi, TFS_BITMAP_BLOCK) != TFS_BITMAP_BLOCK)
		goto err_bmap;

	/* We have all metadata initialized, write everything back to disk */
	error = flush_metadata(tfi);
	if (error) {
		fprintf(stderr, "Unable to flush metadata\n");
		goto err_bmap;
	}
	/* Base filesystem is ready here, time to create the root directory*/

	error = init_rootfs(tfi);
	if (error) {
		fprintf(stderr, "Unable to initialize rootfs directory\n");
		goto err_bmap;
	}

	error = 0;

err_bmap:
	free(tfi->i_arr);
err_i_arr:
	free(tfi->sb_buf);
err_sb_buf:
	close(dfd);
err_bdev_open:
	free(tfi);
err_out:
	exit(error);
}
