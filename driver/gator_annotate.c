/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <linux/spinlock.h>

#define INSTSIZE	1024
static DEFINE_SPINLOCK(annotate_lock);
static char *annotateBuf;
static char *annotateBuf0;
static char *annotateBuf1;
static int annotatePos;
static int annotateSel;

static ssize_t annotate_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	char tempBuffer[32];
	unsigned long flags;
	int retval, remaining, size;

	if (*offset)
		return -EINVAL;

	// determine size to capture
	remaining = INSTSIZE - annotatePos - 24; // leave some extra space
	size = count < sizeof(tempBuffer) ? count : sizeof(tempBuffer);
	size = size < remaining ? size : remaining;
	if (size <= 0)
		return 0;

	// copy from user space
	retval = copy_from_user(tempBuffer, buf, size);
	if (retval == 0) {
		// synchronize shared variables annotateBuf and annotatePos
		spin_lock_irqsave(&annotate_lock, flags);
		if (!annotateBuf) {
			size = -EINVAL;
		} else  {
			*(int*)&annotateBuf[annotatePos + 0] = current->pid;		// thread id
			*(int*)&annotateBuf[annotatePos + 4] = size;				// length in bytes
			memcpy(&annotateBuf[annotatePos + 8], tempBuffer, size);	// data
			annotatePos = annotatePos + 8 + size;						// increment position
			annotatePos = (annotatePos + 3) & ~3;						// align to 4-byte boundary
		}
		spin_unlock_irqrestore(&annotate_lock, flags);

		// return the number of bytes written
		retval = size;
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static const struct file_operations annotate_fops = {
	.write		= annotate_write
};

int gator_annotate_create_files(struct super_block *sb, struct dentry *root)
{
	annotateBuf = annotateBuf0 = annotateBuf1 = NULL;
	return gatorfs_create_file_perm(sb, root, "annotate", &annotate_fops, 0666);
}

int gator_annotate_init(void)
{
	return 0;
}

int gator_annotate_start(void)
{
	annotatePos = annotateSel = 0;
	annotateBuf0 = kmalloc(INSTSIZE, GFP_KERNEL);
	annotateBuf1 = kmalloc(INSTSIZE, GFP_KERNEL);
	annotateBuf = annotateBuf0;
	if (!annotateBuf0 || !annotateBuf1)
		return -1;
	return 0;
}

void gator_annotate_stop(void)
{
	unsigned long flags;
	spin_lock_irqsave(&annotate_lock, flags);

	kfree(annotateBuf0);
	kfree(annotateBuf1);
	annotateBuf = annotateBuf0 = annotateBuf1 = NULL;

	spin_unlock_irqrestore(&annotate_lock, flags);
}

int gator_annotate_read(int **buffer)
{
	int len;

	if (smp_processor_id() || !annotatePos || !annotateBuf)
		return 0;

	annotateSel = !annotateSel;

	if (buffer)
		*buffer = (int *)annotateBuf;

	spin_lock(&annotate_lock);

	len = annotatePos;
	annotatePos = 0;
	annotateBuf = annotateSel ? annotateBuf1 : annotateBuf0;

	spin_unlock(&annotate_lock);

	// Return number of 4-byte words
	return len / 4;
}
