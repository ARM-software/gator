/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */
#include "gator_events_mali_common.h"

static u32 gator_mali_get_id(void)
{
    return MALI_SUPPORT;
}

extern const char* gator_mali_get_mali_name(void)
{
    u32 id = gator_mali_get_id();

    switch (id) {
    case MALI_T6xx:
        return "Mali-T6xx";
    case MALI_400:
        return "Mali-400";
    default:
        pr_debug("gator: Mali-T6xx: unknown Mali ID (%d)\n", id);
        return "Mali-Unknown";
    }
}

extern int gator_mali_create_file_system(const char* mali_name, const char* event_name, struct super_block *sb, struct dentry *root, mali_counter *counter)
{
    int err;
    char buf[255];
    struct dentry *dir;

    /* If the counter name is empty ignore it*/
    if (strlen(event_name) != 0)
    {
        /* Set up the filesystem entry for this event. */
        snprintf(buf, sizeof(buf), "ARM_%s_%s", mali_name, event_name);

        dir = gatorfs_mkdir(sb, root, buf);

        if (dir == NULL)
        {
            pr_debug("gator: Mali-T6xx: error creating file system for: %s (%s)", event_name, buf);
            return -1;
        }

        err = gatorfs_create_ulong(sb, dir, "enabled", &counter->enabled);
        if (err != 0)
        {
            pr_debug("gator: Mali-T6xx: error calling gatorfs_create_ulong for: %s (%s)", event_name, buf);
            return -1;
        }
        err = gatorfs_create_ro_ulong(sb, dir, "key", &counter->key);
        if (err != 0)
        {
            pr_debug("gator: Mali-T6xx: error calling gatorfs_create_ro_ulong for: %s (%s)", event_name, buf);
            return -1;
        }
    }

    return 0;
}

extern void gator_mali_initialise_counters(mali_counter counters[], unsigned int n_counters)
{
    unsigned int cnt;

    for (cnt = 0; cnt < n_counters; cnt++) {
        mali_counter *counter = &counters[cnt];

        counter->key = gator_events_get_key();
        counter->enabled = 0;
    }
}
