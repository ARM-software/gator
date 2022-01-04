/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "GatorCLIParser.h"
#include "GatorException.h"
#include "Logging.h"
#include "OlySocket.h"
#include "ProtocolVersion.h"

#include <array>
#include <sstream>

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

namespace gator::capture::internal {

    constexpr int UDP_REQ_PORT = 30001;

    struct RVIConfigureInfo {
        char rviHeader[8];
        uint32_t messageID;
        uint8_t ethernetAddress[8];
        uint32_t ethernetType;
        uint32_t dhcp;
        char dhcpName[40];
        uint32_t ipAddress;
        uint32_t defaultGateway;
        uint32_t subnetMask;
        uint32_t activeConnections;
    };

    constexpr std::array<const char, 12> DST_REQ = {'D', 'S', 'T', '_', 'R', 'E', 'Q', ' ', 0, 0, 0, 0x64};

    class UdpListener {
    public:
        UdpListener() noexcept : mDstAns(), mReq(-1) {}

        void setup(int port)
        {
            mReq = udpPort(UDP_REQ_PORT);

            // Format the answer buffer
            memset(&mDstAns, 0, sizeof(mDstAns));
            memcpy(mDstAns.rviHeader, "STR_ANS ", sizeof(mDstAns.rviHeader));
            if (gethostname(mDstAns.dhcpName, sizeof(mDstAns.dhcpName) - 1) != 0) {
                if (errno == ENAMETOOLONG) {
                    LOG_DEBUG("Hostname too long, using a default hostname");
                }
                else {
                    // Should be unreachable
                    auto ss = std::stringstream("gethostname failed: (");
                    ss << errno << ") " << strerror(errno);
                    throw GatorException(ss.str());
                }

                strncpy(mDstAns.dhcpName, "Unknown hostname", sizeof(mDstAns.dhcpName));
            }
            // Subvert the defaultGateway field for the port number
            if (port != DEFAULT_PORT) {
                mDstAns.defaultGateway = port;
            }
            // Subvert the subnetMask field for the protocol version
            mDstAns.subnetMask = PROTOCOL_VERSION;
        }

        [[nodiscard]] int getReq() const { return mReq; }

        void handle()
        {
            char buf[128];
            struct sockaddr_in6 sockaddr;
            socklen_t addrlen;
            int read;
            addrlen = sizeof(sockaddr);
            read = recvfrom(mReq, &buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&sockaddr), &addrlen);
            if (read < 0) {
                throw GatorException("recvfrom failed");
            }
            else if ((read == 12) && (memcmp(buf, DST_REQ.data(), DST_REQ.size()) == 0)) {
                // Don't care if sendto fails - gatord shouldn't exit because of it and Streamline will retry
                sendto(mReq, &mDstAns, sizeof(mDstAns), 0, reinterpret_cast<struct sockaddr *>(&sockaddr), addrlen);
            }
        }

        void close() const { ::close(mReq); }

    private:
        static int udpPort(int port)
        {
            int s;
            struct sockaddr_in6 sockaddr;
            int on;
            int family = AF_INET6;

            s = socket_cloexec(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            if (s == -1) {
                family = AF_INET;
                s = socket_cloexec(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (s == -1) {
                    throw GatorException("socket failed");
                }
            }

            on = 1;
            if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
                throw GatorException("setsockopt REUSEADDR failed");
            }

            // Listen on both IPv4 and IPv6
            on = 0;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0) {
                LOG_DEBUG("setsockopt IPV6_V6ONLY failed");
            }

            memset(&sockaddr, 0, sizeof(sockaddr));
            sockaddr.sin6_family = family;
            sockaddr.sin6_port = htons(port);
            sockaddr.sin6_addr = in6addr_any;
            if (bind(s, reinterpret_cast<struct sockaddr *>(&sockaddr), sizeof(sockaddr)) < 0) {
                throw GatorException("socket failed");
            }

            return s;
        }

        RVIConfigureInfo mDstAns;
        int mReq;
    };

} // namespace gator::capture::internal
