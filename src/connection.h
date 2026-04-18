#pragma once
#include <string>

// Holds all state for a single connected client
struct Connection {
    int         fd;
    std::string read_buf;   // accumulates incoming bytes
    std::string write_buf;  // queued responses waiting to be flushed

    explicit Connection(int fd) : fd(fd) {}
};
