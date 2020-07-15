/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#pragma once

namespace armnn {
    class ISession {
    public:
        /**
         * An object which reads and writes to a socket.
         **/
        virtual ~ISession() = default;

        /**
         * Closes the ISession object (interupting the sender and reciever threads)
         **/
        virtual void close() = 0;

        /**
         * Run the recieve thread. This function should be called in a thread outside of the ISession object
         **/
        virtual void runReadLoop() = 0;

        /**
         * Write a packet to the sender queue requesting to start the capture
         **/
        virtual bool enableCapture() = 0;

        /**
         * Write a packet to the sender queue requesting to stop the capture
         **/
        virtual bool disableCapture() = 0;
    };
}