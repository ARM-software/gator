/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "SessionXML.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char TAG_SESSION[] = "session";
static const char TAG_IMAGE[] = "image";

static const char ATTR_VERSION[] = "version";
static const char ATTR_CALL_STACK_UNWINDING[] = "call_stack_unwinding";
static const char ATTR_BUFFER_MODE[] = "buffer_mode";
static const char ATTR_SAMPLE_RATE[] = "sample_rate";
static const char ATTR_DURATION[] = "duration";
static const char USE_EFFICIENT_FTRACE[] = "use_efficient_ftrace";
static const char ATTR_PATH[] = "path";
static const char ATTR_LIVE_RATE[] = "live_rate";
static const char ATTR_CAPTURE_WORKING_DIR[] = "capture_working_dir";
static const char ATTR_CAPTURE_COMMAND[] = "capture_command";
static const char ATTR_CAPTURE_USER[] = "capture_user";

SessionXML::SessionXML(const char *str)
        : parameters(),
          mSessionXML(str)
{
    logg.logMessage("%s", mSessionXML);
}

SessionXML::~SessionXML()
{
}

void SessionXML::parse()
{
    mxml_node_t *tree;
    mxml_node_t *node;

    tree = mxmlLoadString(NULL, mSessionXML, MXML_NO_CALLBACK);
    node = mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND);

    if (node) {
        sessionTag(tree, node);
        mxmlDelete(tree);
        return;
    }

    logg.logError("No session tag found in the session.xml file");
    handleException();
}

void SessionXML::sessionTag(mxml_node_t *tree, mxml_node_t *node)
{
    int version = 0;
    if (mxmlElementGetAttr(node, ATTR_VERSION) && !stringToInt(&version, mxmlElementGetAttr(node, ATTR_VERSION), 10)) {
        logg.logError("Invalid session.xml version must be an integer");
        handleException();
    }

    // Version 2 has only enum-like 'resolution_mode' attribute instead of boolean 'high_resolution' attribute taht version 1 has
    // but none of these are used by gator, so both versions are correctly supported by this implementation.
    if (version < 1 || version > 2) {
        logg.logError("Invalid session.xml version: %d", version);
        handleException();
    }

    // copy to pre-allocated strings
    if (mxmlElementGetAttr(node, ATTR_BUFFER_MODE)) {
        strncpy(parameters.buffer_mode, mxmlElementGetAttr(node, ATTR_BUFFER_MODE), sizeof(parameters.buffer_mode));
        parameters.buffer_mode[sizeof(parameters.buffer_mode) - 1] = 0; // strncpy does not guarantee a null-terminated string
    }
    if (mxmlElementGetAttr(node, ATTR_SAMPLE_RATE)) {
        strncpy(parameters.sample_rate, mxmlElementGetAttr(node, ATTR_SAMPLE_RATE), sizeof(parameters.sample_rate));
        parameters.sample_rate[sizeof(parameters.sample_rate) - 1] = 0; // strncpy does not guarantee a null-terminated string
    }
    if (mxmlElementGetAttr(node, ATTR_CAPTURE_WORKING_DIR))
        gSessionData.mCaptureWorkingDir = strdup(mxmlElementGetAttr(node, ATTR_CAPTURE_WORKING_DIR));
    if (mxmlElementGetAttr(node, ATTR_CAPTURE_COMMAND))
        gSessionData.mCaptureCommand = strdup(mxmlElementGetAttr(node, ATTR_CAPTURE_COMMAND));
    if (mxmlElementGetAttr(node, ATTR_CAPTURE_USER))
        gSessionData.mCaptureUser = strdup(mxmlElementGetAttr(node, ATTR_CAPTURE_USER));

    // integers/bools
    parameters.call_stack_unwinding = stringToBool(mxmlElementGetAttr(node, ATTR_CALL_STACK_UNWINDING), false);
    if (mxmlElementGetAttr(node, ATTR_DURATION)
            && !stringToInt(&gSessionData.mDuration, mxmlElementGetAttr(node, ATTR_DURATION), 10)) {
        logg.logError("Invalid session.xml duration must be an integer");
        handleException();
    }
    gSessionData.mFtraceRaw = stringToBool(mxmlElementGetAttr(node, USE_EFFICIENT_FTRACE), false);
    if (mxmlElementGetAttr(node, ATTR_LIVE_RATE)
            && !stringToInt(&parameters.live_rate, mxmlElementGetAttr(node, ATTR_LIVE_RATE), 10)) {
        logg.logError("Invalid session.xml live_rate must be an integer");
        handleException();
    }

    // parse subtags
    node = mxmlGetFirstChild(node);
    while (node) {
        if (mxmlGetType(node) != MXML_ELEMENT) {
            node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
            continue;
        }
        if (strcmp(TAG_IMAGE, mxmlGetElement(node)) == 0) {
            sessionImage(node);
        }
        node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
    }
}

void SessionXML::sessionImage(mxml_node_t *node)
{
    gSessionData.mImages.emplace_back(mxmlElementGetAttr(node, ATTR_PATH));
}
