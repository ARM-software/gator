/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/IAcceptingSocket.h"
#include "armnn/ISocketIOConsumer.h"

#include <condition_variable>
#include <list>
#include <memory>
#include <queue>

namespace armnn {

    /**
     * @brief This class acts as a hand-off point from agent-worker threads
     * responsible for accepting UDS sockets, to a thread responsible for
     * creating Session objects from newly accepted sockets (represented
     * with the ISocketIO interface).
     *
     * Newly accepted sockets are deposited on an internal queue via the
     * consumeSocket method.
     *
     * ISocketIOs are taken off the queue via the accept method.
     */
    class AcceptedSocketQueue : public IAcceptingSocket, public ISocketIOConsumer {
    public:
        // Take a socket of the internal queue
        // Blocks if the queue is empty.
        [[nodiscard]] std::unique_ptr<ISocketIO> accept(int /* timeout */) override
        {
            // The timeout is only significant for implementations of IAcceptingSocket that
            // ultimately call the accept() syscall with the supplied timeout.  That is being
            // done elsewhere. Therefore ignore the timeout.

            std::unique_lock<std::mutex> lock {mutex};

            wait_until_non_empty(lock);

            if (interrupt_count > 0) {
                return nullptr;
            }

            std::unique_ptr<ISocketIO> result = std::move(internal.front());
            internal.pop();

            return result;
        }

        void interrupt() override
        {
            {
                const std::unique_lock<std::mutex> lock {mutex};
                ++interrupt_count;
            }
            non_empty_queue_cv.notify_one();
        };

        void consumeSocket(std::unique_ptr<ISocketIO> ptr) override
        {
            {
                const std::unique_lock<std::mutex> lock {mutex};
                internal.push(std::move(ptr));
            }
            non_empty_queue_cv.notify_one();
        }

        [[nodiscard]] std::size_t size() const { return internal.size(); }

        ~AcceptedSocketQueue() override = default;

    private:
        using socket_ptr = std::unique_ptr<ISocketIO>;
        std::queue<socket_ptr, std::list<socket_ptr>> internal {};
        std::mutex mutex {};
        std::condition_variable non_empty_queue_cv {};
        uint32_t interrupt_count {0};

        void wait_until_non_empty(std::unique_lock<std::mutex> & lock)
        {
            while (internal.empty() && interrupt_count == 0) {
                non_empty_queue_cv.wait(lock);
            }
        }
    };

}
