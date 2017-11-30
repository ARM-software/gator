/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef CLASSBOILERPLATE_H_
#define CLASSBOILERPLATE_H_

/**
 * Delete the copy constructor/assignment operator
 */
#define CLASS_DELETE_COPY(CLASSNAME)                    \
    CLASSNAME(const CLASSNAME &) = delete;              \
    CLASSNAME& operator= (const CLASSNAME &) = delete

/**
 * Delete the move constructor/assignment operator
 */
#define CLASS_DELETE_MOVE(CLASSNAME)                    \
    CLASSNAME(CLASSNAME &&) = delete;                   \
    CLASSNAME& operator= (CLASSNAME &&) = delete

/**
 * Delete the copy and move constructors/assignment operators
 */
#define CLASS_DELETE_COPY_MOVE(CLASSNAME)               \
    CLASS_DELETE_COPY(CLASSNAME);                       \
    CLASS_DELETE_MOVE(CLASSNAME)


/**
 * Default the copy constructor/assignment operator
 */
#define CLASS_DEFAULT_COPY(CLASSNAME)                    \
    CLASSNAME(const CLASSNAME &) = default;              \
    CLASSNAME& operator= (const CLASSNAME &) = default

/**
 * Default the move constructor/assignment operator
 */
#define CLASS_DEFAULT_MOVE(CLASSNAME)                    \
    CLASSNAME(CLASSNAME &&) = default;                   \
    CLASSNAME& operator= (CLASSNAME &&) = default

/**
 * Default the copy and move constructors/assignment operators
 */
#define CLASS_DEFAULT_COPY_MOVE(CLASSNAME)               \
    CLASS_DEFAULT_COPY(CLASSNAME);                       \
    CLASS_DEFAULT_MOVE(CLASSNAME)

#endif /* CLASSBOILERPLATE_H_ */
