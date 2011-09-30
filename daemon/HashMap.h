/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef	__HASH_MAP_H__
#define	__HASH_MAP_H__

/**********************************
This class is a limited and lossy hash map, where each hash table bucket will contain at most MAX_COLLISIONS entries
If the limit is exceeded, one of the old entries is dropped from the table
This limit eliminates the need for dynamic memory allocation
It is efficient with a data set containing a lot of use-only-once data
Zero is used as an invalid (unused) hash entry value
**********************************/

#define HASHMAP_ENTRIES		1024		/* must be power of 2 */
#define MAX_COLLISIONS		2

class HashMap {
public:
	HashMap();
	~HashMap();
	bool existsAdd(int value);
private:
	int *hashEntries(int key);
	int *history;
};

#endif //__HASH_MAP_H__
