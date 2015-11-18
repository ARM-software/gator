struct gator_cpu {
	struct list_head list;
	unsigned long cpuid;
	unsigned long pmnc_counters;
	/* Human readable name */
	char core_name[MAXSIZE_CORE_NAME];
	/* gatorfs event and Perf PMU name */
	char pmnc_name[MAXSIZE_CORE_NAME];
	/* compatible from Documentation/devicetree/bindings/arm/cpus.txt */
	char dt_name[MAXSIZE_CORE_NAME];
};

struct uncore_pmu {
	struct list_head list;
	unsigned long pmnc_counters;
	unsigned long has_cycles_counter;
	/* Perf PMU name */
	char pmnc_name[MAXSIZE_CORE_NAME];
	/* gatorfs event name */
	char core_name[MAXSIZE_CORE_NAME];
};

static LIST_HEAD(uncore_pmus);
static LIST_HEAD(gator_cpus);
static DEFINE_MUTEX(pmu_mutex);

static struct super_block *gator_sb;
static struct dentry *gator_events_dir;

static const struct gator_cpu *gator_find_cpu_by_cpuid(const u32 cpuid)
{
	const struct gator_cpu *gator_cpu;

	list_for_each_entry(gator_cpu, &gator_cpus, list) {
		if (gator_cpu->cpuid == cpuid)
			return gator_cpu;
	}

	return NULL;
}

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

__maybe_unused
static const struct gator_cpu *gator_find_cpu_by_pmu_name(const char *const name)
{
	const struct gator_cpu *gator_cpu;

	list_for_each_entry(gator_cpu, &gator_cpus, list) {
		if (gator_cpu->pmnc_name != NULL &&
		    /* Do the names match exactly? */
		    (strcasecmp(gator_cpu->pmnc_name, name) == 0 ||
		     /* Do these names match but have the old vs new prefix? */
		     ((strncasecmp(name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) == 0 &&
		       strncasecmp(gator_cpu->pmnc_name, NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) == 0 &&
		       strcasecmp(name + sizeof(OLD_PMU_PREFIX) - 1, gator_cpu->pmnc_name + sizeof(NEW_PMU_PREFIX) - 1) == 0))))
			return gator_cpu;
	}

	return NULL;
}

__maybe_unused
static const struct uncore_pmu *gator_find_uncore_pmu(const char *const name)
{
	const struct uncore_pmu *uncore_pmu;

	list_for_each_entry(uncore_pmu, &uncore_pmus, list) {
		if (uncore_pmu->pmnc_name != NULL && strcasecmp(uncore_pmu->pmnc_name, name) == 0)
			return uncore_pmu;
	}

	return NULL;
}

static bool gator_pmu_initialized;

static ssize_t gator_pmu_init_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	if (gator_pmu_initialized)
		return -EINVAL;
	gator_pmu_initialized = true;
	if (gator_events_perf_pmu_reread() != 0 ||
			gator_events_perf_pmu_create_files(gator_sb, gator_events_dir) != 0)
		return -EINVAL;
	return count;
}

static const struct file_operations gator_pmu_init_fops = {
	.write = gator_pmu_init_write,
};

static ssize_t gator_pmu_str_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	char *const val = file->private_data;

	return simple_read_from_buffer(buf, count, offset, val, strlen(val));
}

static ssize_t gator_pmu_str_write_file(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	char *value = file->private_data;

	if (*offset)
		return -EINVAL;

	if (count >= MAXSIZE_CORE_NAME)
		return -EINVAL;
	if (copy_from_user(value, buf, count))
		return -EFAULT;
	value[count] = 0;
	value = strstrip(value);

	return count;
}

static const struct file_operations gator_pmu_str_fops = {
	.read = gator_pmu_str_read_file,
	.write = gator_pmu_str_write_file,
	.open = default_open,
};

static int gator_pmu_create_str(struct super_block *sb, struct dentry *root, char const *name, char *const val)
{
	struct dentry *d = __gatorfs_create_file(sb, root, name, &gator_pmu_str_fops, 0644);
	if (!d)
		return -EFAULT;

	d->d_inode->i_private = val;
	return 0;
}

static ssize_t gator_pmu_export_write(struct file *file, char const __user *ubuf, size_t count, loff_t *offset)
{
	struct dentry *dir;
	struct dentry *parent;
	char buf[MAXSIZE_CORE_NAME];
	const char *str;

	if (*offset)
		return -EINVAL;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(&buf, ubuf, count))
		return -EFAULT;
	buf[count] = 0;
	str = strstrip(buf);

	parent = file->f_path.dentry->d_parent;
	dir = gatorfs_mkdir(gator_sb, parent, buf);
	if (!dir)
		return -EINVAL;

	if (strcmp("pmu", parent->d_name.name) == 0) {
		struct gator_cpu *gator_cpu;

		gator_cpu = kmalloc(sizeof(*gator_cpu), GFP_KERNEL);
		if (gator_cpu == NULL)
			return -ENOMEM;
		memset(gator_cpu, 0, sizeof(*gator_cpu));

		gatorfs_create_ulong(gator_sb, dir, "cpuid", &gator_cpu->cpuid);
		gator_pmu_create_str(gator_sb, dir, "core_name", gator_cpu->core_name);
		strcpy(gator_cpu->pmnc_name, str);
		gator_pmu_create_str(gator_sb, dir, "dt_name", gator_cpu->dt_name);
		gatorfs_create_ulong(gator_sb, dir, "pmnc_counters", &gator_cpu->pmnc_counters);

		mutex_lock(&pmu_mutex);
		list_add_tail(&gator_cpu->list, &gator_cpus); /* mutex */
		mutex_unlock(&pmu_mutex);
	} else {
		struct uncore_pmu *uncore_pmu;

		uncore_pmu = kmalloc(sizeof(*uncore_pmu), GFP_KERNEL);
		if (uncore_pmu == NULL)
			return -ENOMEM;
		memset(uncore_pmu, 0, sizeof(*uncore_pmu));

		strcpy(uncore_pmu->pmnc_name, str);
		gator_pmu_create_str(gator_sb, dir, "core_name", uncore_pmu->core_name);
		gatorfs_create_ulong(gator_sb, dir, "pmnc_counters", &uncore_pmu->pmnc_counters);
		gatorfs_create_ulong(gator_sb, dir, "has_cycles_counter", &uncore_pmu->has_cycles_counter);

		mutex_lock(&pmu_mutex);
		list_add_tail(&uncore_pmu->list, &uncore_pmus); /* mutex */
		mutex_unlock(&pmu_mutex);
	}

	return count;
}

static const struct file_operations export_fops = {
	.write = gator_pmu_export_write,
};

static int gator_pmu_create_files(struct super_block *sb, struct dentry *root, struct dentry *events)
{
	struct dentry *dir;

	gator_sb = sb;
	gator_events_dir = events;

	gatorfs_create_file(sb, root, "pmu_init", &gator_pmu_init_fops);

	dir = gatorfs_mkdir(sb, root, "pmu");
	if (!dir)
		return -1;

	gatorfs_create_file(sb, dir, "export", &export_fops);

	dir = gatorfs_mkdir(sb, root, "uncore_pmu");
	if (!dir)
		return -1;

	gatorfs_create_file(sb, dir, "export", &export_fops);

	return 0;
}

static void gator_pmu_exit(void)
{
	mutex_lock(&pmu_mutex);
	{
		struct gator_cpu *gator_cpu;
		struct gator_cpu *next;

		list_for_each_entry_safe(gator_cpu, next, &gator_cpus, list) {
			kfree(gator_cpu);
		}
	}
	{
		struct uncore_pmu *uncore_pmu;
		struct uncore_pmu *next;

		list_for_each_entry_safe(uncore_pmu, next, &uncore_pmus, list) {
			kfree(uncore_pmu);
		}
	}
	mutex_unlock(&pmu_mutex);
}
