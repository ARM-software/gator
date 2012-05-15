/**

 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__CAPTURED_XML_H__
#define	__CAPTURED_XML_H__

#include "mxml/mxml.h"

class CapturedXML {
public:
	CapturedXML();
	~CapturedXML();
	char* getXML(); // the string should be freed by the caller
	void write(char* path);
private:
	mxml_node_t* getTree();
};

#endif 	//__CAPTURED_XML_H__
