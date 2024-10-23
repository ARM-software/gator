/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_SYSCALL_H
#define INCLUDE_LIB_SYSCALL_H

#include <array>

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

struct perf_event_attr;

struct sockaddr;

struct utsname;

namespace lib {
    int close(int fd);

    int open(const char * path, int flag);

    int open(const char * path, int flag, mode_t mode);

    int fcntl(int fd, int cmd, unsigned long arg = 0);

    int ioctl(int fd, unsigned long request, unsigned long arg);

    void * mmap(void * addr, size_t length, int prot, int flags, int fd, off_t offset);
    int munmap(void * addr, size_t length);

    int perf_event_open(struct perf_event_attr * attr, pid_t pid, int cpu, int group_fd, unsigned long flags);

    int accept4(int sockfd, struct sockaddr * addr, socklen_t * addrlen, int flags);

    ssize_t read(int fd, void * buf, size_t count);
    ssize_t write(int fd, const void * buf, size_t count);

    int pipe2(std::array<int, 2> & fds, int flags);

    int uname(struct utsname * buf);

    uid_t geteuid();

    pid_t waitpid(pid_t pid, int * wstatus, int options);

    int poll(struct pollfd * fds, nfds_t nfds, int timeout);
    int access(const char * filename, int how);
    void exit(int status);

    int kill(pid_t pid, int signal);

    pid_t getppid();
    pid_t getpid();
    pid_t gettid();
}

#endif // INCLUDE_LIB_SYSCALL_H
