/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

namespace armnn {
    /**
     * Interface that should be adopted by a handler of capture sessions
     **/
    class ICaptureStartStopHandler {
    public:
        virtual ~ICaptureStartStopHandler() = default;

        /**
         * Enable a capture on all capture sessions that the derived class handles
         **/
        virtual void startCapture() = 0;

        /**
         * Disable a capture on all capture sessions that the derived class handles
         * Should be called before the end of the derived class's lifecycle (given the capture has been started).
         **/
        virtual void stopCapture() = 0;
    };
}
