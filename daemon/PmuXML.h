/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PMUXML_H
#define PMUXML_H

class PmuXML {
public:
	PmuXML();
	~PmuXML();

	static void read(const char *const path);
	static void writeToKernel();

private:
	static void parse(const char *const xml);
	static void getDefaultXml(const char **const xml, unsigned int *const len);

	// Intentionally unimplemented
	PmuXML(const PmuXML &);
	PmuXML &operator=(const PmuXML &);
};

#endif // PMUXML_H
