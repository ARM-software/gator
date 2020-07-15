/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef __FIFO_H__
#define __FIFO_H__

#ifdef WIN32
#include <windows.h>
#define sem_t HANDLE
#define sem_init(sem, pshared, value) ((*(sem) = CreateSemaphore(nullptr, value, LONG_MAX, nullptr)) == nullptr)
#define sem_wait(sem) WaitForSingleObject(*(sem), INFINITE)
#define sem_post(sem) ReleaseSemaphore(*(sem), 1, nullptr)
#define sem_destroy(sem) CloseHandle(*(sem))
#else
#include <semaphore.h>
#endif

class Fifo {
public:
    Fifo(int singleBufferSize, int totalBufferSize, sem_t * readerSem);
    ~Fifo();
    int numBytesFilled() const;
    bool isEmpty() const;
    bool isFull() const;
    bool willFill(int additional) const;
    char * start() const;
    char * write(int length);
    void release();
    char * read(int * length);

private:
    int mSingleBufferSize, mWrite, mRead, mReadCommit, mRaggedEnd, mWrapThreshold;
    sem_t mWaitForSpaceSem;
    sem_t * mReaderSem;
    char * mBuffer;
    bool mEnd;

    // Intentionally unimplemented
    Fifo(const Fifo &) = delete;
    Fifo & operator=(const Fifo &) = delete;
    Fifo(Fifo &&) = delete;
    Fifo & operator=(Fifo &&) = delete;
};

#endif //__FIFO_H__
