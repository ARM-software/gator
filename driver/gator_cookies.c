/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define COOKIEMAP_ENTRIES	1024		/* must be power of 2 */
#define MAX_COLLISIONS		2

static DEFINE_PER_CPU(uint32_t, cookie_next_key);
static DEFINE_PER_CPU(uint64_t *, cookie_keys);
static DEFINE_PER_CPU(uint32_t *, cookie_values);

static uint32_t *gator_crc32_table;

static uint32_t cookiemap_code(uint32_t value) {
	uint32_t cookiecode = (value >> 24) & 0xff;
	cookiecode = cookiecode * 31 + ((value >> 16) & 0xff);
	cookiecode = cookiecode * 31 + ((value >> 8) & 0xff);
	cookiecode = cookiecode * 31 + ((value >> 0) & 0xff);
	cookiecode &= (COOKIEMAP_ENTRIES-1);
	return cookiecode * MAX_COLLISIONS;
}

static uint32_t gator_chksum_crc32(char *data)
{
   register unsigned long crc;
   unsigned char *block = data;
   int i, length = strlen(data);

   crc = 0xFFFFFFFF;
   for (i = 0; i < length; i++) {
      crc = ((crc >> 8) & 0x00FFFFFF) ^ gator_crc32_table[(crc ^ *block++) & 0xFF];
   }

   return (crc ^ 0xFFFFFFFF);
}

/*
 * Exists
 *  Pre:  [0][1][v][3]..[n-1]
 *  Post: [v][0][1][3]..[n-1]
 */
static uint32_t cookiemap_exists(uint64_t key) {
	int cpu = raw_smp_processor_id();
	uint32_t cookiecode = cookiemap_code(key);
	uint64_t *keys = &(per_cpu(cookie_keys, cpu)[cookiecode]);
	uint32_t *values = &(per_cpu(cookie_values, cpu)[cookiecode]);
	int x;

	for (x = 0; x < MAX_COLLISIONS; x++) {
		if (keys[x] == key) {
			uint32_t value = values[x];
			for (; x > 0; x--) {
				keys[x] = keys[x-1];
				values[x] = values[x-1];
			}
			keys[0] = key;
			values[0] = value;
			return value;
		}
	}

	return 0;
}

/*
 * Add
 *  Pre:  [0][1][2][3]..[n-1]
 *  Post: [v][0][1][2]..[n-2]
 */
static void cookiemap_add(uint64_t key, uint32_t value) {
	int cpu = raw_smp_processor_id();
	int cookiecode = cookiemap_code(key);
	uint64_t *keys = &(per_cpu(cookie_keys, cpu)[cookiecode]);
	uint32_t *values = &(per_cpu(cookie_values, cpu)[cookiecode]);
	int x;

	for (x = MAX_COLLISIONS-1; x > 0; x--) {
		keys[x] = keys[x-1];
		values[x] = keys[x-1];
	}
	keys[0] = key;
	values[0] = value;
}

static inline uint32_t get_cookie(int cpu, int tgid, struct vm_area_struct *vma)
{
	struct path *path;
	uint64_t key;
	int cookie;
	char *text;

	if (!vma || !vma->vm_file) {
		return INVALID_COOKIE;
	}
	path = &vma->vm_file->f_path;
	if (!path || !path->dentry) {
		return INVALID_COOKIE;
	}

	text = (char*)path->dentry->d_name.name;
	key = gator_chksum_crc32(text);
	key = (key << 32) | (uint32_t)text;

	cookie = cookiemap_exists(key);
	if (cookie) {
		goto output;
	}

	cookie = per_cpu(cookie_next_key, cpu)+=nr_cpu_ids;
	cookiemap_add(key, cookie);

	gator_buffer_write_packed_int(cpu, PROTOCOL_COOKIE);
	gator_buffer_write_packed_int(cpu, cookie);
	gator_buffer_write_string(cpu, text);

output:
	return cookie;
}

static int get_exec_cookie(int cpu, struct task_struct *task)
{
	unsigned long cookie = NO_COOKIE;
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;

	if (!mm)
		return cookie;

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (!vma->vm_file)
			continue;
		if (!(vma->vm_flags & VM_EXECUTABLE))
			continue;
		cookie = get_cookie(cpu, task->tgid, vma);
		break;
	}

	return cookie;
}

static unsigned long get_address_cookie(int cpu, struct task_struct *task, unsigned long addr, off_t *offset)
{
	unsigned long cookie = NO_COOKIE;
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;

	if (!mm)
		return cookie;

	for (vma = find_vma(mm, addr); vma; vma = vma->vm_next) {
		if (addr < vma->vm_start || addr >= vma->vm_end)
			continue;

		if (vma->vm_file) {
			cookie = get_cookie(cpu, task->tgid, vma);
			*offset = (vma->vm_pgoff << PAGE_SHIFT) + addr - vma->vm_start;
		} else {
			/* must be an anonymous map */
			*offset = addr;
		}

		break;
	}

	if (!vma)
		cookie = INVALID_COOKIE;

	return cookie;
}

static void cookies_initialize(void)
{
	uint32_t crc, poly;
	int cpu, size;
	int i, j;

	for_each_present_cpu(cpu) {
		per_cpu(cookie_next_key, cpu) = nr_cpu_ids + cpu;

		size = COOKIEMAP_ENTRIES * MAX_COLLISIONS * sizeof(uint64_t);
		per_cpu(cookie_keys, cpu) = (uint64_t*)kmalloc(size, GFP_KERNEL);
		memset(per_cpu(cookie_keys, cpu), 0, size);

		size = COOKIEMAP_ENTRIES * MAX_COLLISIONS * sizeof(uint32_t);
		per_cpu(cookie_values, cpu) = (uint32_t*)kmalloc(size, GFP_KERNEL);
		memset(per_cpu(cookie_values, cpu), 0, size);
	}

	// build CRC32 table
	poly = 0x04c11db7;
	gator_crc32_table = (uint32_t*)kmalloc(256 * sizeof(uint32_t), GFP_KERNEL);
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1) {
				crc = (crc >> 1) ^ poly;
			} else {
				crc >>= 1;
			}
		}
		gator_crc32_table[i] = crc;
	}
}

static void cookies_release(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		kfree(per_cpu(cookie_keys, cpu));
		per_cpu(cookie_keys, cpu) = NULL;

		kfree(per_cpu(cookie_values, cpu));
		per_cpu(cookie_values, cpu) = NULL;
	}

	kfree(gator_crc32_table);
	gator_crc32_table = NULL;
}
