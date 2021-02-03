/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "CurrentConfigXML.h"

#include "MxmlUtils.h"

namespace current_config_xml {
    static constexpr char TAG_CURRENT_CONFIG[] = "current_config";
    static constexpr char TAG_PIDS_TO_CAPTURE[] = "pids_to_capture";
    static constexpr char TAG_STATE[] = "state";

    static constexpr char PID[] = "pid";
    static constexpr char ATTR_UID[] = "uid";
    static constexpr char ATTR_VALUE[] = "value";
    static constexpr char ATTR_SYSTEM_WIDE[] = "is_system_wide";
    static constexpr char ATTR_WAITING_ON_CMD[] = "is_waiting_on_command";
    static constexpr char ATTR_WAIT_FOR_PROCESS[] = "wait_for_process";
    static constexpr char ATTR_CAPTURE_WORKING_DIR[] = "capture_working_directory";

    std::string generateCurrentConfigXML(std::int32_t pid,
                                         std::uint32_t uid,
                                         bool isSystemWide,
                                         bool isWaitingOnCommand,
                                         const char * waitForProcessCommand,
                                         const char * captureWorkingDir,
                                         std::set<int> & pidsToCapture)
    {
        // Construct current config XML
        mxml_unique_ptr currentConfigNode {makeMxmlUniquePtr(mxmlNewElement(MXML_NO_PARENT, TAG_CURRENT_CONFIG))};

        mxmlElementSetAttrf(currentConfigNode.get(), PID, "%i", pid);
        mxmlElementSetAttrf(currentConfigNode.get(), ATTR_UID, "%u", uid);

        mxml_unique_ptr stateNode {makeMxmlUniquePtr(mxmlNewElement(currentConfigNode.get(), TAG_STATE))};
        if (isSystemWide) {
            mxmlElementSetAttr(stateNode.get(), ATTR_SYSTEM_WIDE, "yes");
        }
        else {
            mxmlElementSetAttr(stateNode.get(), ATTR_SYSTEM_WIDE, "no");
        }

        if (isWaitingOnCommand) {
            mxmlElementSetAttr(stateNode.get(), ATTR_WAITING_ON_CMD, "yes");
        }
        else {
            mxmlElementSetAttr(stateNode.get(), ATTR_WAITING_ON_CMD, "no");
        }

        if (waitForProcessCommand != nullptr) {
            mxmlElementSetAttr(stateNode.get(), ATTR_WAIT_FOR_PROCESS, waitForProcessCommand);
        }

        if (captureWorkingDir != nullptr) {
            mxmlElementSetAttr(stateNode.get(), ATTR_CAPTURE_WORKING_DIR, captureWorkingDir);
        }

        // Loop through the pids to capture and add to XML
        mxml_unique_ptr pidsToCapNode {makeMxmlUniquePtr(nullptr)};
        if (!pidsToCapture.empty()) {
            pidsToCapNode.reset(mxmlNewElement(currentConfigNode.get(), TAG_PIDS_TO_CAPTURE));
            for (int pid : pidsToCapture) {
                auto pidToCap = mxmlNewElement(pidsToCapNode.get(), PID);
                mxmlElementSetAttrf(pidToCap, ATTR_VALUE, "%i", pid);
            }
        }

        return mxmlSaveAsStdString(currentConfigNode.get(), mxmlWhitespaceCB);
    }
};