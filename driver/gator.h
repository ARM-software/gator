/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GATOR_H_
#define GATOR_H_

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/list.h>

/******************************************************************************
 * Filesystem
 ******************************************************************************/
int gatorfs_create_file_perm(struct super_block *sb, struct dentry *root,
	char const *name, const struct file_operations *fops, int perm);

struct dentry *gatorfs_mkdir(struct super_block *sb,
	struct dentry *root, char const *name);

int gatorfs_create_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val);

int gatorfs_create_ro_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val);

void gator_op_create_files(struct super_block *sb, struct dentry *root);

/******************************************************************************
 * Tracepoints
 ******************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#	error Kernels prior to 2.6.32 not supported
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
#	define GATOR_DEFINE_PROBE(probe_name, proto) \
		static void probe_##probe_name(PARAMS(proto))
#	define GATOR_REGISTER_TRACE(probe_name) \
		register_trace_##probe_name(probe_##probe_name)
#	define GATOR_UNREGISTER_TRACE(probe_name) \
		unregister_trace_##probe_name(probe_##probe_name)
#else
#	define GATOR_DEFINE_PROBE(probe_name, proto) \
		static void probe_##probe_name(void *data, PARAMS(proto))
#	define GATOR_REGISTER_TRACE(probe_name) \
		register_trace_##probe_name(probe_##probe_name, NULL)
#	define GATOR_UNREGISTER_TRACE(probe_name) \
		unregister_trace_##probe_name(probe_##probe_name, NULL)
#endif

/******************************************************************************
 * Events
 ******************************************************************************/
struct gator_interface {
	int  (*create_files)(struct super_block *sb, struct dentry *root);
	int  (*start)(void);
	void (*stop)(void);
	void (*online)(void);
	void (*offline)(void);
	int  (*read)(int **buffer);
	struct list_head list;
};

#define gator_events_init(initfn) \
	static inline int __gator_events_init_test(void) \
	{ return initfn(); }

int gator_events_install(struct gator_interface *interface);
int gator_events_get_key(void);
extern u32 gator_cpuid(void);

extern unsigned long gator_net_traffic;


#endif // GATOR_H_
