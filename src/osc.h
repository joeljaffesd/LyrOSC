#pragma once

#include <string>
#include <cstdint>

// Minimal OSC-over-UDP sender.
// Encodes a single string argument to a given address pattern and sends it.
class OscSender {
public:
    OscSender();
    ~OscSender();

    bool open(const std::string& host, uint16_t port);
    void close();
    bool send(const std::string& address, const std::string& value);

private:
    int sock_ = -1;
};
