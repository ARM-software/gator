/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "Syscall.h"

#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

namespace lib
{
    int close(int fd) {
        return ::close(fd);
    }
    int open(const char* path, int flag)  {
        return ::open(path, flag);
    }
    int fcntl(int fd, int cmd, unsigned long arg) {
        return ::fcntl(fd, cmd, arg);
    }

    int ioctl(int fd, unsigned long int request, unsigned long arg)
    {
        return ::ioctl(fd, request, arg);
    }

    void * mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    {
        return ::mmap(addr, length, prot, flags, fd, offset);
    }

    int munmap(void *addr, size_t length)
    {
        return ::munmap(addr, length);
    }

    int perf_event_open(struct perf_event_attr * const attr, const pid_t pid, const int cpu, const int group_fd, const unsigned long flags)
    {
        return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
    }

    int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
    {
        return syscall(__NR_accept4, sockfd, addr, addrlen, flags);
    }

    ssize_t read(int fd, void *buf, size_t count)
    {
        return ::read(fd, buf, count);
    }

    int uname(struct utsname *buf)
    {
        return ::uname(buf);
    }

    uid_t geteuid()
    {
        return ::geteuid();
    }

    int poll (struct pollfd * fds, nfds_t nfds, int timeout) {
        return ::poll(fds, nfds, timeout);
    }
}

