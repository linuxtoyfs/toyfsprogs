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

void display_help(void)
{
	printf("Choose an option: --\n");
	printf("\t q  - quit\n");
	printf("\t s  - print superblock\n");
	printf("\t si - print inode usage\n");
	printf("\t h  - show this message\n");
}

void display_inode_usage(void)
{
	int i = 0;
	struct tfs_dinode	*inode_array;
	struct tfs_dsb		*sb;

	inode_array = malloc(TFS_INODE_COUNT * sizeof(struct tfs_dinode));
	sb = malloc(sizeof(struct tfs_dsb));

	lseek(dfd, TFS_SB_BLOCK * TFS_BSIZE, SEEK_SET);
	read(dfd, sb, sizeof(struct tfs_dsb));

	lseek(dfd, TFS_INODE_BLOCK * TFS_BSIZE, SEEK_SET);
	read(dfd, inode_array, TFS_INODE_COUNT * sizeof(struct tfs_dinode));

	printf("====================\n");
	for (i = 0; i < TFS_INODE_COUNT; i++) {
		printf("Num: %u - ", i);
		if (sb->s_inodes[i] && TFS_INODE_INUSE) {
			printf("l: %u u: %u g: %u s: %u b: %u\n",
			       inode_array[i].i_nlink,
			       inode_array[i].i_uid,
			       inode_array[i].i_gid,
			       inode_array[i].i_size,
			       inode_array[i].i_blocks);
		} else {
			printf("FREE\n");
		}
	}
	free(sb);
	free(inode_array);
}

void display_super(char *cmd)
{
	struct tfs_dsb *d_super = malloc(sizeof(struct tfs_dsb));

	lseek(dfd, TFS_SB_BLOCK * TFS_BSIZE, SEEK_SET);
	read(dfd, d_super, sizeof(struct tfs_dsb));

	switch (cmd[1]) {
		case '\0':
			printf("====================\n");
			printf("s_magic: 0x%x\n", d_super->s_magic);
			printf("s_flags: %s\n", d_super->s_flags ==
						TFS_SB_CLEAN ?
						"SB_CLEAN" :
						"SB_DIRTY");
			printf("Free inodes: %u\n", d_super->s_nifree);
			printf("Free blocks: %u\n", d_super->s_nbfree);
			printf("====================\n");
			break;
		case 'i':
			display_inode_usage();
			break;
		default:
			printf("Invalid superblock option\n");
	}

	free(d_super);
}

void display_dir_block(char *cmd)
{
	struct tfs_dentry *dentry;
	int blk = 0;
	int i = 0;
	int max_entries = (TFS_BSIZE / sizeof(struct tfs_dentry));

	dentry = malloc(sizeof(struct tfs_dentry) * max_entries);
	blk = atoi(&cmd[1]);
	lseek(dfd, blk * TFS_BSIZE, SEEK_SET);

	read(dfd, dentry, max_entries * sizeof(struct tfs_dentry));

	while(1) {
		if (dentry[i].d_name[i] == '\0')
			break;
		printf("Inode: %u - name: %s\n",
		       dentry[i].d_ino, dentry[i].d_name);
		i++;
	}

	free(dentry);
}

int main(int argc, char **argv)
{
	//struct tfs_dentry root_dir;
	char	cmd[10];
	int	err = -1;
	printf("Size of tfs_dinode: %lu\n", sizeof(struct tfs_dinode));

	if (argc != 2) {
		fprintf(stderr, "Please specify block device\n");
		return 1;
	}

	/*
	 * XXX We need better error handling and
	 * better check of the underlying device
	 * We also must ensure we're dealing with
	 * either a block device or a regular file
	 */
	dfd = open(argv[1], O_RDWR);

	if (dfd < 0) {
		fprintf(stderr, "Error opening device");
		return 1;
	}
	printf("Device opened successfully\n");

	while (1) {
		printf("toyfs_db > ");
		fflush(stdout);
		err = scanf("%s", cmd);

		if (err < 0)
			printf("Something went wrong\n");

		switch(cmd[0]) {
			case 's':
				display_super(cmd);
				break;
			case 'd':
				display_dir_block(cmd);
				break;
			case 'q': return 0; break;
			case 'h':
				display_help();
				break;
			default: printf("Wrong option\n");
		}
	}
}
