/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#include "SessionXML.h"

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace {
    constexpr const char * TAG_SESSION = "session";
    constexpr const char * TAG_IMAGE = "image";

    constexpr const char * ATTR_VERSION = "version";
    constexpr const char * ATTR_CALL_STACK_UNWINDING = "call_stack_unwinding";
    constexpr const char * ATTR_BUFFER_MODE = "buffer_mode";
    constexpr const char * ATTR_SAMPLE_RATE = "sample_rate";
    constexpr const char * ATTR_DURATION = "duration";
    constexpr const char * USE_EFFICIENT_FTRACE = "use_efficient_ftrace";
    constexpr const char * ATTR_PATH = "path";
    constexpr const char * ATTR_LIVE_RATE = "live_rate";
    constexpr const char * ATTR_CAPTURE_WORKING_DIR = "capture_working_dir";
    constexpr const char * ATTR_CAPTURE_COMMAND = "capture_command";
    constexpr const char * ATTR_STOP_GATOR = "stop_gator";
    constexpr const char * ATTR_CAPTURE_USER = "capture_user";
    constexpr const char * ATTR_EXCLUDE_KERNEL_EVENTS = "exclude_kernel_events";
}

SessionXML::SessionXML(const char * str) : mSessionXML(str)
{
    logg.logMessage("%s", mSessionXML);
}

void SessionXML::parse()
{
    auto * const tree = mxmlLoadString(nullptr, mSessionXML, MXML_NO_CALLBACK);
    auto * const node = mxmlFindElement(tree, tree, TAG_SESSION, nullptr, nullptr, MXML_DESCEND);

    if (node != nullptr) {
        sessionTag(tree, node);
        mxmlDelete(tree);
        return;
    }

    logg.logError("No session tag found in the session.xml file");
    handleException();
}

void SessionXML::sessionTag(mxml_node_t * tree, mxml_node_t * node)
{
    int version = 0;
    if ((mxmlElementGetAttr(node, ATTR_VERSION) != nullptr) &&
        !stringToInt(&version, mxmlElementGetAttr(node, ATTR_VERSION), 10)) {
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
    if (mxmlElementGetAttr(node, ATTR_BUFFER_MODE) != nullptr) {
        strncpy(parameters.buffer_mode, mxmlElementGetAttr(node, ATTR_BUFFER_MODE), sizeof(parameters.buffer_mode));
        parameters.buffer_mode[sizeof(parameters.buffer_mode) - 1] =
            0; // strncpy does not guarantee a null-terminated string
    }
    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_SAMPLE_RATE) == 0)) {
        if (mxmlElementGetAttr(node, ATTR_SAMPLE_RATE) != nullptr) {
            strncpy(parameters.sample_rate, mxmlElementGetAttr(node, ATTR_SAMPLE_RATE), sizeof(parameters.sample_rate));
            parameters.sample_rate[sizeof(parameters.sample_rate) - 1] =
                0; // strncpy does not guarantee a null-terminated string
        }
    }
    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_WORKING_DIR) == 0)) {
        if (mxmlElementGetAttr(node, ATTR_CAPTURE_WORKING_DIR) != nullptr) {
            if (gSessionData.mCaptureWorkingDir != nullptr) {
                free(const_cast<char *>(gSessionData.mCaptureWorkingDir));
            }
            gSessionData.mCaptureWorkingDir = strdup(mxmlElementGetAttr(node, ATTR_CAPTURE_WORKING_DIR));
        }
    }

    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) == 0) &&
        (mxmlElementGetAttr(node, ATTR_CAPTURE_COMMAND) != nullptr)) {
        //sh and -c added for shell interpreted execution of the command
        gSessionData.mCaptureCommand.emplace_back("sh");
        gSessionData.mCaptureCommand.emplace_back("-c");
        gSessionData.mCaptureCommand.emplace_back(mxmlElementGetAttr(node, ATTR_CAPTURE_COMMAND));
    }
    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_STOP_GATOR) == 0)) {
        if (mxmlElementGetAttr(node, ATTR_STOP_GATOR) != nullptr) {
            gSessionData.mStopOnExit = stringToBool(mxmlElementGetAttr(node, ATTR_STOP_GATOR), false);
        }
    }
    if (mxmlElementGetAttr(node, ATTR_CAPTURE_USER) != nullptr) {
        if (gSessionData.mCaptureUser != nullptr) {
            free(const_cast<char *>(gSessionData.mCaptureUser));
        }
        gSessionData.mCaptureUser = strdup(mxmlElementGetAttr(node, ATTR_CAPTURE_USER));
    }

    // integers/bools
    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) == 0)) {
        parameters.call_stack_unwinding = stringToBool(mxmlElementGetAttr(node, ATTR_CALL_STACK_UNWINDING), false);
    }
    if (((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_DURATION) == 0)) {
        if (mxmlElementGetAttr(node, ATTR_DURATION) != nullptr) {
            if (!stringToInt(&gSessionData.mDuration, mxmlElementGetAttr(node, ATTR_DURATION), 10)) {
                logg.logError("Invalid session.xml duration must be an integer");
                handleException();
            }
        }
    }
    if ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_FTRACE_RAW) == 0) {
        gSessionData.mFtraceRaw =
            stringToBool(mxmlElementGetAttr(node, USE_EFFICIENT_FTRACE), false); // default to false
    }
    if (mxmlElementGetAttr(node, ATTR_LIVE_RATE) != nullptr) {
        if (!stringToInt(&parameters.live_rate, mxmlElementGetAttr(node, ATTR_LIVE_RATE), 10)) {
            logg.logError("Invalid session.xml live_rate must be an integer");
            handleException();
        }
    }
    if ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_EXCLUDE_KERNEL) == 0) {
        gSessionData.mExcludeKernelEvents = stringToBool(mxmlElementGetAttr(node, ATTR_EXCLUDE_KERNEL_EVENTS), false);
    }

    // parse subtags
    node = mxmlGetFirstChild(node);
    while (node != nullptr) {
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

void SessionXML::sessionImage(mxml_node_t * node)
{
    gSessionData.mImages.emplace_back(mxmlElementGetAttr(node, ATTR_PATH));
}
