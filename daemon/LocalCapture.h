/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LOCAL_CAPTURE_H__
#define __LOCAL_CAPTURE_H__

#include <list>
#include <string>


namespace local_capture
{
    void copyImages(const std::list<std::string> & ptr);
    void createAPCDirectory(const char* target_path);
    int removeDirAndAllContents(const char* path);
};

#endif //__LOCAL_CAPTURE_H__
