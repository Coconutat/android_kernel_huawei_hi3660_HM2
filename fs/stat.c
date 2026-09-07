/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs_def.h>
#endif

void generic_fillattr(struct inode *inode, struct kstat *stat)
{
	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = inode->i_rdev;
	stat->size = i_size_read(inode);
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->blksize = i_blocksize(inode);
	stat->blocks = inode->i_blocks;
