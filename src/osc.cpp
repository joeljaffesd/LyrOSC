#include "osc.h"

#include <cstring>
#include <stdexcept>
#include <vector>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

// OSC strings are null-terminated and padded to 4-byte alignment.
static void append_osc_string(std::vector<uint8_t>& buf, const std::string& s) {
    for (char c : s) buf.push_back(static_cast<uint8_t>(c));
    buf.push_back(0);
    while (buf.size() % 4 != 0) buf.push_back(0);
}

OscSender::OscSender() = default;

OscSender::~OscSender() {
    close();
}

bool OscSender::open(const std::string& host, uint16_t port) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        ::close(sock_);
        sock_ = -1;
        return false;
    }
    if (connect(sock_, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ::close(sock_);
        sock_ = -1;
        return false;
    }
    freeaddrinfo(res);
    return true;
}

void OscSender::close() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool OscSender::send(const std::string& address, const std::string& value) {
    if (sock_ < 0) return false;

    std::vector<uint8_t> packet;
    packet.reserve(256);

    append_osc_string(packet, address);
    append_osc_string(packet, ",s");
    append_osc_string(packet, value);

    return ::send(sock_, packet.data(), packet.size(), 0) == static_cast<ssize_t>(packet.size());
}
