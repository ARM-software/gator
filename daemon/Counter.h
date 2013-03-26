/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTER_H
#define COUNTER_H

#include <string.h>

class Driver;

class Counter {
public:
	static const size_t MAX_STRING_LEN = 80;
	static const size_t MAX_DESCRIPTION_LEN = 400;

	Counter () {
		clear();
	}

	void clear () {
		mType[0] = '\0';
		mTitle[0] = '\0';
		mName[0] = '\0';
		mDescription[0] = '\0';
		mDisplay[0] = '\0';
		mUnits[0] = '\0';
		mModifier = 1;
		mEnabled = false;
		mEvent = 0;
		mCount = 0;
		mKey = 0;
		mPerCPU = false;
		mEBSCapable = false;
		mAverageSelection = false;
		mDriver = NULL;
	}

	void setType(const char *const type) { strncpy(mType, type, sizeof(mType)); mType[sizeof(mType) - 1] = '\0'; }
	void setTitle(const char *const title) { strncpy(mTitle, title, sizeof(mTitle)); mTitle[sizeof(mTitle) - 1] = '\0'; }
	void setName(const char *const name) { strncpy(mName, name, sizeof(mName)); mName[sizeof(mName) - 1] = '\0'; }
	void setDescription(const char *const description) { strncpy(mDescription, description, sizeof(mDescription)); mDescription[sizeof(mDescription) - 1] = '\0'; }
	void setDisplay(const char *const display) { strncpy(mDisplay, display, sizeof(mDisplay)); mDisplay[sizeof(mDisplay) - 1] = '\0'; }
	void setUnits(const char *const units) { strncpy(mUnits, units, sizeof(mUnits)); mUnits[sizeof(mUnits) - 1] = '\0'; }
	void setModifier(const int modifier) { mModifier = modifier; }
	void setEnabled(const bool enabled) { mEnabled = enabled; }
	void setEvent(const int event) { mEvent = event; }
	void setCount(const int count) { mCount = count; }
	void setKey(const int key) { mKey = key; }
	void setPerCPU(const bool perCPU) { mPerCPU = perCPU; }
	void setEBSCapable(const bool ebsCapable) { mEBSCapable = ebsCapable; }
	void setAverageSelection(const bool averageSelection) { mAverageSelection = averageSelection; }
	void setDriver(Driver *const driver) { mDriver = driver; }

	const char *getType() const { return mType;}
	const char *getTitle() const { return mTitle; }
	const char *getName() const { return mName; }
	const char *getDescription() const { return mDescription; }
	const char *getDisplay() const { return mDisplay; }
	const char *getUnits() const { return mUnits; }
	int getModifier() const { return mModifier; }
	bool isEnabled() const { return mEnabled; }
	int getEvent() const { return mEvent; }
	int getCount() const { return mCount; }
	int getKey() const { return mKey; }
	bool isPerCPU() const { return mPerCPU; }
	bool isEBSCapable() const { return mEBSCapable; }
	bool isAverageSelection() const { return mAverageSelection; }
	Driver *getDriver() const { return mDriver; }

private:
	// Intentionally unimplemented
	Counter(const Counter &);
	Counter & operator=(const Counter &);

	char mType[MAX_STRING_LEN];
	char mTitle[MAX_STRING_LEN];
	char mName[MAX_STRING_LEN];
	char mDescription[MAX_DESCRIPTION_LEN];
	char mDisplay[MAX_STRING_LEN];
	char mUnits[MAX_STRING_LEN];
	int mModifier;
	bool mEnabled;
	int mEvent;
	int mCount;
	int mKey;
	bool mPerCPU;
	bool mEBSCapable;
	bool mAverageSelection;
	Driver *mDriver;
};

#endif // COUNTER_H
