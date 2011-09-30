/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include "HashMap.h"

/*
 * LRU Lossy HashMap
 * Values are always inserted to first slot
 * Value hits are moved to the first slot
 */

HashMap::HashMap() {
	history = (int*)calloc(HASHMAP_ENTRIES * MAX_COLLISIONS, sizeof(int));
}

HashMap::~HashMap() {
	free(history);
}

int *HashMap::hashEntries(int value) {
	int hashCode = (value >> 24) & 0xff;
	hashCode = hashCode * 31 + ((value >> 16) & 0xff);
	hashCode = hashCode * 31 + ((value >> 8) & 0xff);
	hashCode = hashCode * 31 + ((value >> 0) & 0xff);
	hashCode &= (HASHMAP_ENTRIES-1);
	return &history[hashCode * MAX_COLLISIONS];
}

/*
 * Exists
 *  Pre:  [0][1][v][3]..[n-1]
 *  Post: [v][0][1][3]..[n-1]
 * Add
 *  Pre:  [0][1][2][3]..[n-1]
 *  Post: [v][0][1][2]..[n-2]
 */
bool HashMap::existsAdd(int value) {
	int *line = hashEntries(value);

	/* exists */
	for (int x = 0; x < MAX_COLLISIONS; x++) {
		if (line[x] == value) {
			for (; x > 0; x--) {
				line[x] = line[x-1];
			}
			line[0] = value;
			return true;
		}
	}

	/* add */
	for (int x = MAX_COLLISIONS-1; x > 0; x--) {
		line[x] = line[x-1];
	}
	line[0] = value;

	return false;
}
