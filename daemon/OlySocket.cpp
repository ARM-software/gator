/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "OlySocket.h"

#include "lib/Error.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cstddef>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "Logging.h"
#include "lib/Syscall.h"

#ifdef WIN32
#define CLOSE_SOCKET(x) closesocket(x)
#define SHUTDOWN_RX_TX SD_BOTH
#define snprintf _snprintf
#else
#define CLOSE_SOCKET(x) close(x)
#define SHUTDOWN_RX_TX SHUT_RDWR
#endif

int socket_cloexec(int domain, int type, int protocol)
{
    int sock;

#ifdef SOCK_CLOEXEC
    /* Try create socket as SOCK_CLOEXEC */
    sock = socket(domain, type | SOCK_CLOEXEC, protocol); // NOLINT(hicpp-signed-bitwise)
    if (sock >= 0) {
        return sock;
    }
    //NOLINTNEXTLINE(concurrency-mt-unsafe)
    LOG_WARNING("Failed socket %i/%i/%i CLOEXEC due to %i %s", domain, type, protocol, errno, lib::strerror());

#endif

    /* Try create socket */
    sock = socket(domain, type, protocol);
    if (sock < 0) {
        LOG_WARNING("Failed socket {domain = %i, type = %i, protocol = %i} due to %i (%s)",
                    domain,
                    type,
                    protocol,
                    errno,
                    lib::strerror());
        return -1;
    }

    /* Try to set CLOEXEC */
#ifdef FD_CLOEXEC
    int fdf = fcntl(sock, F_GETFD);
    if ((fdf == -1) || (fcntl(sock, F_SETFD, fdf | FD_CLOEXEC) != 0)) { // NOLINT(hicpp-signed-bitwise)
        LOG_WARNING("Failed FD_CLOEXEC on {domain = %i, type = %i, protocol = %i, socket = %i, fd = %i} due to %i (%s)",
                    domain,
                    type,
                    protocol,
                    sock,
                    fdf,
                    errno,
                    //NOLINTNEXTLINE(concurrency-mt-unsafe)
                    lib::strerror());
        close(sock);
        return -1;
    }
#endif

    return sock;
}

int accept_cloexec(int sockfd, struct sockaddr * addr, socklen_t * addrlen)
{
    int sock;
#ifdef SOCK_CLOEXEC
    sock = lib::accept4(sockfd, addr, addrlen, SOCK_CLOEXEC);
    if (sock >= 0) {
        return sock;
    }
    // accept4 with SOCK_CLOEXEC may not work on all kernels, so fallback
#endif
    sock = accept(sockfd, addr, addrlen); // NOLINT(android-cloexec-accept)
#ifdef FD_CLOEXEC
    if (sock < 0) {
        return -1;
    }
    int fdf = fcntl(sock, F_GETFD);
    if ((fdf == -1) || (fcntl(sock, F_SETFD, fdf | FD_CLOEXEC) != 0)) { // NOLINT(hicpp-signed-bitwise)
        close(sock);
        return -1;
    }
#endif
    return sock;
}

OlyServerSocket::OlyServerSocket(int port) : mFDServer(0)
{
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(0x0202, &wsaData) != 0) {
        LOG_ERROR("Windows socket initialization failed");
        handleException();
    }
#endif

    createServerSocket(port);
}

OlySocket::OlySocket(int socketID)
    : mSocketID(socketID) {}

#ifndef WIN32

#define MIN(A, B)                                                                                                      \
    ({                                                                                                                 \
        const __typeof__(A) __a = A;                                                                                   \
        const __typeof__(B) __b = B;                                                                                   \
        __a > __b ? __b : __a;                                                                                         \
    })

      OlyServerSocket::OlyServerSocket(const char * path, const size_t pathSize, const bool calculateAddrlen)
    : mFDServer(0)
{
    // Create socket
    mFDServer = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
    if (mFDServer < 0) {
        LOG_ERROR("Error creating server unix socket");
        handleException();
    }

    // Create sockaddr_in structure, ensuring non-populated fields are zero
    struct sockaddr_un sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sun_family = AF_UNIX;
    memcpy(sockaddr.sun_path, path, MIN(pathSize, sizeof(sockaddr.sun_path)));
    sockaddr.sun_path[sizeof(sockaddr.sun_path) - 1] = '\0';

    // Bind the socket to an address
    if (bind(mFDServer,
             reinterpret_cast<const struct sockaddr *>(&sockaddr),
             calculateAddrlen ? offsetof(struct sockaddr_un, sun_path) + pathSize - 1 : sizeof(sockaddr))
        < 0) {
        //                                                                    use sun_path because it is null terminated
        //                                                                    if path was actually empty string
        //                                                                    vv
        const char * const printablePath = path[0] != '\0' ? path : &sockaddr.sun_path[1];
        LOG_ERROR("Binding of server socket to '%s' failed: %s", printablePath, lib::strerror());
        handleException();
    }

    // Listen for connections on this socket
    if (listen(mFDServer, 1) < 0) {
        LOG_ERROR("Listening of server socket failed");
        handleException();
    }
}

int OlySocket::connect(const char * path, const size_t pathSize)
{
    int fd = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // Create sockaddr_in structure, ensuring non-populated fields are zero
    struct sockaddr_un sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sun_family = AF_UNIX;
    memcpy(sockaddr.sun_path, path, MIN(pathSize, sizeof(sockaddr.sun_path)));
    sockaddr.sun_path[sizeof(sockaddr.sun_path) - 1] = '\0';

    if (::connect(fd, reinterpret_cast<const struct sockaddr *>(&sockaddr), sizeof(sockaddr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

#endif

OlySocket::~OlySocket()
{
    if (mSocketID > 0) {
        CLOSE_SOCKET(mSocketID);
    }
}

OlyServerSocket::~OlyServerSocket()
{
    if (mFDServer > 0) {
        CLOSE_SOCKET(mFDServer);
    }
}

void OlySocket::shutdownConnection() // NOLINT(readability-make-member-function-const)
{
    // Shutdown is primarily used to unblock other threads that are blocking on send/receive functions
    shutdown(mSocketID, SHUTDOWN_RX_TX);
}

void OlySocket::closeSocket()
{
    // Used for closing an accepted socket but keeping the server socket active
    if (mSocketID > 0) {
        CLOSE_SOCKET(mSocketID);
        mSocketID = -1;
    }
}

void OlyServerSocket::closeServerSocket()
{
    if (mFDServer > 0 && CLOSE_SOCKET(mFDServer) != 0) {
        LOG_ERROR("Failed to close server socket.");
        handleException();
    }
    mFDServer = -1;
}

void OlyServerSocket::createServerSocket(int port)
{
    int family = AF_INET6;

    // Create socket
    mFDServer = socket_cloexec(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (mFDServer < 0) {
        family = AF_INET;
        mFDServer = socket_cloexec(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (mFDServer < 0) {
            LOG_ERROR("Error creating server TCP socket (%d : %s)", errno, lib::strerror());
            handleException();
        }
    }

    // Enable address reuse, another solution would be to create the server socket once and only close it when the object exits
    int on = 1;
    if (setsockopt(mFDServer, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        LOG_ERROR("Setting server socket reuse option failed");
        handleException();
    }

    // Listen on both IPv4 and IPv6
    on = 0;
    if (setsockopt(mFDServer, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0) {
        LOG_FINE("setsockopt IPV6_V6ONLY failed");
    }

    // Create sockaddr_in structure, ensuring non-populated fields are zero
    struct sockaddr_in6 sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin6_family = family;
    sockaddr.sin6_port = htons(port);
    sockaddr.sin6_addr = in6addr_any;

    // Bind the socket to an address
    if (bind(mFDServer, reinterpret_cast<const struct sockaddr *>(&sockaddr), sizeof(sockaddr)) < 0) {
        LOG_ERROR("Binding of server socket on port %i failed.\nIs an instance already running or is another "
                  "application using that port?",
                  port);
        handleException();
    }

    // Listen for connections on this socket
    if (listen(mFDServer, 1) < 0) {
        LOG_ERROR("Listening of server socket failed");
        handleException();
    }
}

// mSocketID is always set to the most recently accepted connection
// The user of this class should maintain the different socket connections, e.g. by forking the process
int OlyServerSocket::acceptConnection() // NOLINT(readability-make-member-function-const)
{
    int socketID;
    if (mFDServer <= 0) {
        LOG_ERROR("Attempting multiple connections on a single connection server socket or attempting to accept on "
                  "a client socket");
        handleException();
    }

    // Accept a connection, note that this call blocks until a client connects
    socketID = accept_cloexec(mFDServer, nullptr, nullptr);
    if (socketID < 0) {
        LOG_ERROR("Socket acceptance failed");
        handleException();
    }
    return socketID;
}

void OlySocket::send(const uint8_t * buffer, int size) // NOLINT(readability-make-member-function-const)
{
    if (size <= 0 || buffer == nullptr) {
        return;
    }

    while (size > 0) {
        int n = ::send(mSocketID, buffer, size, 0);
        if (n < 0) {
            LOG_ERROR("Socket send error (%d): %s", errno, lib::strerror());
            handleException();
        }
        size -= n;
        buffer += n;
    }
}

// Returns the number of bytes received
int OlySocket::receive(uint8_t * buffer, int size) // NOLINT(readability-make-member-function-const)
{
    if (size <= 0 || buffer == nullptr) {
        return 0;
    }

    int bytes = recv(mSocketID, buffer, size, 0);
    if (bytes < 0) {
        LOG_ERROR("Socket receive error (%d): %s", errno, lib::strerror());
        handleException();
    }
    else if (bytes == 0) {
        LOG_FINE("Socket disconnected");
        return -1;
    }
    return bytes;
}

// Receive exactly size bytes of data. Note, this function will block until all bytes are received
int OlySocket::receiveNBytes(uint8_t * buffer, int size) // NOLINT(readability-make-member-function-const)
{
    int bytes = 0;
    while (size > 0 && buffer != nullptr) {
        bytes = recv(mSocketID, buffer, size, 0);
        if (bytes < 0) {
            LOG_ERROR("Socket receive error (%d): %s", errno, lib::strerror());
            handleException();
        }
        else if (bytes == 0) {
            LOG_FINE("Socket disconnected");
            return -1;
        }
        buffer += bytes;
        size -= bytes;
    }
    return bytes;
}

// Receive data until a carriage return, line feed, or null is encountered, or the buffer fills
int OlySocket::receiveString(uint8_t * buffer, int size) // NOLINT(readability-make-member-function-const)
{
    int bytes_received = 0;
    bool found = false;

    if (buffer == nullptr) {
        return 0;
    }

    while (!found && bytes_received < size) {
        // Receive a single character
        int bytes = recv(mSocketID, &buffer[bytes_received], 1, 0);
        if (bytes < 0) {
            LOG_ERROR("Socket receive error (%d): %s", errno, lib::strerror());
            handleException();
        }
        else if (bytes == 0) {
            LOG_FINE("Socket disconnected");
            return -1;
        }

        // Replace carriage returns and line feeds with zero
        if (buffer[bytes_received] == '\n' || buffer[bytes_received] == '\r' || buffer[bytes_received] == '\0') {
            buffer[bytes_received] = '\0';
            found = true;
        }

        bytes_received++;
    }

    return bytes_received;
}
