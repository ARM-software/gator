/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef __OLY_SOCKET_H__
#define __OLY_SOCKET_H__

#include <cstddef>
#include <cstdint>

#ifdef WIN32
using socklen_t = int;
#else
#include <sys/socket.h>
#endif

class OlySocket {
public:
#ifndef WIN32
    /**
     * @brief Connect to a unix domain socket (as per libc's connect function)
     *
     * @param path Unix domain socket path (as per sockaddr_un::sun_path). An abstract socket can be specified with an initial null-byte
     * @param pathSize Size of @a path including the null terminator
     * @return int 0 for success, -1 for failure
     */
    static int connect(const char * path, size_t pathSize);
#endif

    OlySocket(int socketID);
    ~OlySocket();

    void closeSocket();
    void shutdownConnection();
    void send(const uint8_t * buffer, int size);
    int receive(uint8_t * buffer, int size);
    int receiveNBytes(uint8_t * buffer, int size);
    int receiveString(uint8_t * buffer, int size);

    [[nodiscard]] bool isValid() const { return mSocketID >= 0; }

    [[nodiscard]] int getFd() const { return mSocketID; }

private:
    int mSocketID;
};

class OlyServerSocket {
public:
    OlyServerSocket(int port);
#ifndef WIN32
    OlyServerSocket(const char * path, size_t pathSize, bool calculateAddrlen = false);
#endif
    ~OlyServerSocket();

    int acceptConnection();
    void closeServerSocket();

    int getFd() const { return mFDServer; }

private:
    int mFDServer;

    void createServerSocket(int port);
};

int socket_cloexec(int domain, int type, int protocol);
int accept_cloexec(int sockfd, struct sockaddr * addr, socklen_t * addrlen);

#endif //__OLY_SOCKET_H__
