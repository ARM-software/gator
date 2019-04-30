/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_THROW_H
#define INCLUDE_THROW_H

#ifdef __EXCEPTIONS
#define GATOR_THROW(exception) (throw (exception))
#else
#include <stdlib.h>
#define GATOR_THROW(exception) (::abort())
#endif

#endif // INCLUDE_THROW_H

