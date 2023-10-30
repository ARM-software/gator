/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */
#include "armnn/ThreadManagementServer.h"

#include "Logging.h"

#include <cassert>

namespace armnn {
    ThreadManagementServer::ThreadManagementServer(std::unique_ptr<IAcceptor> acceptor)
        : mAcceptor {std::move(acceptor)}
    {
    }

    void ThreadManagementServer::stop()
    {
        if (mIsRunning) {

            // Interrupt the acceptor incase it is blocking
            mAcceptor->interrupt();
            if (mAcceptorThread.joinable()) {
                mAcceptorThread.join();
            }

            // Ensure that stopCapture has been called
            assert(!mEnabled);

            // Shut down the threads
            std::unique_lock<std::mutex> lock {mMutex};
            closeThreads();
            mDone = true;
            lock.unlock();
            mSessionDiedCV.notify_all();
            if (mReaperThread.joinable()) {
                mReaperThread.join();
            }
            mIsRunning = false;
        }
    }

    void ThreadManagementServer::start()
    {
        mReaperThread = std::thread {&ThreadManagementServer::reaperLoop, this};
        mAcceptorThread = std::thread {&ThreadManagementServer::acceptLoop, this};
    }

    void ThreadManagementServer::startCapture()
    {
        std::unique_lock<std::mutex> lock {mMutex};
        for (auto & t : mThreads) {
            t.session->enableCapture();
        }
        mEnabled = true;
    }

    void ThreadManagementServer::stopCapture()
    {
        const std::unique_lock<std::mutex> lock {mMutex};
        for (auto & t : mThreads) {
            t.session->disableCapture();
        }
        mEnabled = false;
    }

    void ThreadManagementServer::addThreadToVector(ThreadData threadData)
    {
        mThreads.push_back(std::move(threadData));
    }

    void ThreadManagementServer::closeThreads()
    {
        for (auto & t : mThreads) {
            t.session->close();
        }
    }

    void ThreadManagementServer::runIndividualThread(ISession & session, bool & done)
    {
        session.runReadLoop();
        std::unique_lock<std::mutex> lock {mMutex};
        done = true;
        lock.unlock();
        mSessionDiedCV.notify_all();
    }

    void ThreadManagementServer::acceptLoop()
    {
        LOG_DEBUG("Started armnn session accept loop");
        while (true) {
            if (!acceptSession(mAcceptor->accept())) {
                return;
            }
        }
        LOG_DEBUG("armnn session accept loop finished");
    }

    bool ThreadManagementServer::acceptSession(std::unique_ptr<ISession> session)
    {
        if (!session) {
            return false;
        }

        const std::lock_guard<std::mutex> lock {mMutex};

        if (!mIsRunning) {
            session->close();
            return false;
        }

        if (mEnabled) {
            session->enableCapture();
        }
        else {
            session->disableCapture();
        }

        // Create the ThreadData and add it to the vector
        std::unique_ptr<bool> done {new bool {false}};
        ISession & sessionRef = *session;
        bool & doneRef = *done;
        std::unique_ptr<std::thread> t {new std::thread {&ThreadManagementServer::runIndividualThread,
                                                         this,
                                                         std::ref(sessionRef),
                                                         std::ref(doneRef)}};

        addThreadToVector({std::move(t), std::move(session), std::move(done)});

        return true;
    }

    void ThreadManagementServer::reaperLoop()
    {
        LOG_DEBUG("Started armnn reaper loop");
        std::unique_lock<std::mutex> lock {mMutex};
        removeCompletedThreads();
        while (!mDone || !mThreads.empty()) {
            // Wait for a Session to die, remove it from the vector
            mSessionDiedCV.wait(lock);
            // Whem the TMS is done, remove all the threads from the vector
            removeCompletedThreads();
        }
        LOG_DEBUG("armnn reaper loop finished");
    }

    void ThreadManagementServer::removeCompletedThreads()
    {
        for (auto t = mThreads.begin(); t != mThreads.end();) {
            bool isThreadDone = *t->done;
            if (isThreadDone) {
                t->thread->join();
                t = mThreads.erase(t);
            }
            else {
                ++t;
            }
        }
    }
}
