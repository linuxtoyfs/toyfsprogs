# SPDX-License-Identifier: GPL-2.0-only
all:
	gcc -Wall -o mkfs -iquote ./include/ ./mkfs.c
clean:
	rm -rf mkfs.o
	rm -rf mkfs
