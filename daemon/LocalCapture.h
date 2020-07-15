/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef __LOCAL_CAPTURE_H__
#define __LOCAL_CAPTURE_H__

#include <list>
#include <string>

namespace local_capture {
    void copyImages(const std::list<std::string> & list);
    void createAPCDirectory(const char * target_path);
    int removeDirAndAllContents(const char * path);
};

#endif //__LOCAL_CAPTURE_H__
