/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_SYSCALL_H
#define INCLUDE_LIB_SYSCALL_H

#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>


struct perf_event_attr;

struct sockaddr;

struct utsname;

namespace lib
{
    int close(int fd);

    int open(const char* path, int flag);

    int fcntl(int fd, int cmd, unsigned long arg = 0);

    int ioctl(int fd, unsigned long request, unsigned long arg);

    void * mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    int munmap(void *addr, size_t length);

    int perf_event_open(struct perf_event_attr * const attr, const pid_t pid, const int cpu, const int group_fd, const unsigned long flags);

    int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);

    ssize_t read(int fd, void *buf, size_t count);

    int uname(struct utsname *buf);

    uid_t geteuid();

    int poll (struct pollfd *__fds, nfds_t __nfds, int __timeout);

}

#endif // INCLUDE_LIB_SYSCALL_H

