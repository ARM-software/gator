/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

#define ANNOTATE_SIZE	(16*1024)
static DEFINE_SPINLOCK(annotate_lock);
static char *annotateBuf;
static char *annotateBuf0;
static char *annotateBuf1;
static int annotatePos;
static int annotateSel;
static bool collect_annotations = false;

static ssize_t annotate_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	char tempBuffer[512];
	int remaining, size;
	uint32_t tid;

	if (*offset)
		return -EINVAL;

	// determine size to capture
	size = count < sizeof(tempBuffer) ? count : sizeof(tempBuffer);

	// note: copy may be for naught if remaining is zero, but better to do the copy outside of the spinlock
	if (file == NULL) {
		// copy from kernel
		memcpy(tempBuffer, buf, size);

		// set the thread id to the kernel thread, not the current thread
		tid = -1;
	} else {
		// copy from user space
		if (copy_from_user(tempBuffer, buf, size) != 0)
			return -EINVAL;
		tid = current->pid;
	}

	// synchronize shared variables annotateBuf and annotatePos
	spin_lock(&annotate_lock);
	if (collect_annotations && annotateBuf) {
		remaining = ANNOTATE_SIZE - annotatePos - 256; // pad for headers and release
		size = size < remaining ? size : remaining;
		if (size > 0) {
			uint64_t time = gator_get_time();
			uint32_t cpuid = smp_processor_id();
			int pos = annotatePos;
			pos += gator_write_packed_int(&annotateBuf[pos], tid);
			pos += gator_write_packed_int64(&annotateBuf[pos], time);
			pos += gator_write_packed_int(&annotateBuf[pos], cpuid);
			pos += gator_write_packed_int(&annotateBuf[pos], size);
			memcpy(&annotateBuf[pos], tempBuffer, size);
			annotatePos = pos + size;
		}
	}
	spin_unlock(&annotate_lock);

	if (size <= 0) {
		wake_up(&gator_buffer_wait);
		return 0;
	}

	// return the number of bytes written
	return size;
}

#include "gator_annotate_kernel.c"

static int annotate_release(struct inode *inode, struct file *file)
{
	int remaining = ANNOTATE_SIZE - annotatePos;
	if (remaining < 16) {
		return -EFAULT;
	}

	spin_lock(&annotate_lock);
	if (annotateBuf) {
		uint32_t tid = current->pid;
		int pos = annotatePos;
		pos += gator_write_packed_int(&annotateBuf[pos], tid);
		pos += gator_write_packed_int64(&annotateBuf[pos], 0); // time
		pos += gator_write_packed_int(&annotateBuf[pos], 0); // cpuid
		pos += gator_write_packed_int(&annotateBuf[pos], 0); // size
		annotatePos = pos;
	}
	spin_unlock(&annotate_lock);

	return 0;
}

static const struct file_operations annotate_fops = {
	.write		= annotate_write,
	.release	= annotate_release
};

static int gator_annotate_create_files(struct super_block *sb, struct dentry *root)
{
	annotateBuf = NULL;
	return gatorfs_create_file_perm(sb, root, "annotate", &annotate_fops, 0666);
}

static int gator_annotate_init(void)
{
	annotateBuf0 = kmalloc(ANNOTATE_SIZE, GFP_KERNEL);
	annotateBuf1 = kmalloc(ANNOTATE_SIZE, GFP_KERNEL);
	if (!annotateBuf0 || !annotateBuf1)
		return -1;
	return 0;
}

static int gator_annotate_start(void)
{
	annotateSel = 0;
	annotatePos = 1;
	annotateBuf = annotateBuf0;
	annotateBuf[0] = FRAME_ANNOTATE;
	collect_annotations = true;
	return 0;
}

static void gator_annotate_stop(void)
{
	collect_annotations = false;
}

static void gator_annotate_shutdown(void)
{
	spin_lock(&annotate_lock);
	annotateBuf = NULL;
	spin_unlock(&annotate_lock);
}

static void gator_annotate_exit(void)
{
	spin_lock(&annotate_lock);
	kfree(annotateBuf0);
	kfree(annotateBuf1);
	annotateBuf = annotateBuf0 = annotateBuf1 = NULL;
	spin_unlock(&annotate_lock);
}

static int gator_annotate_ready(void)
{
	return annotatePos > 1 && annotateBuf;
}

static int gator_annotate_read(char **buffer)
{
	int len;

	if (!gator_annotate_ready())
		return 0;

	annotateSel = !annotateSel;

	if (buffer)
		*buffer = annotateBuf;

	spin_lock(&annotate_lock);
	len = annotatePos;
	annotateBuf = annotateSel ? annotateBuf1 : annotateBuf0;
	annotateBuf[0] = FRAME_ANNOTATE;
	annotatePos = 1;
	spin_unlock(&annotate_lock);

	return len;
}
