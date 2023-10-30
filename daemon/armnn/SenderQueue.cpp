/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "armnn/SenderQueue.h"

#include "Logging.h"

namespace armnn {

    bool SenderQueue::add(std::vector<std::uint8_t> && data)
    {
        std::unique_lock<std::mutex> queueLock {mQueueMutex};
        if (mSendFinished) {
            return false;
        }
        mQueue.push(std::move(data));
        queueLock.unlock();
        mConditionVar.notify_one();
        return true;
    }

    void SenderQueue::stopSending()
    {
        std::unique_lock<std::mutex> queueLock {mQueueMutex};
        mSendFinished = true;
        queueLock.unlock();
        mConditionVar.notify_one();
    }

    void SenderQueue::sendLoop()
    {
        LOG_DEBUG("Start of sender loop");
        // send packets from the queue as and when they become available.
        while (true) {
            std::unique_lock<std::mutex> queueLock {mQueueMutex};
            if (mSendFinished) {
                break;
            }

            // Wait for something to be put in the queue
            if (mQueue.empty()) {
                mConditionVar.wait(queueLock);
            }

            // Make sure that mSendFinished wasn't changed during the condition_variable
            // Also ensure the queue is not empty, could be a spurious wake up from condition_variable
            if (!mSendFinished && !mQueue.empty()) {
                std::vector<std::uint8_t> data {std::move(mQueue.front())};
                mQueue.pop();
                queueLock.unlock();

                sendItem(std::move(data));
            }
        }
        LOG_DEBUG("Exit sender loop");
    }

    void SenderQueue::sendItem(std::vector<std::uint8_t> && data)
    {
        if (!mConnection.writeExact(data)) {
            unableToSendItem();
        }
    }

    void SenderQueue::unableToSendItem()
    {
        LOG_ERROR("Unable to send packet");
        stopSending();
    }
}
