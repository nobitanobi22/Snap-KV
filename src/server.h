#pragma once
#include "store.h"
#include "connection.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <sys/epoll.h>

class Server {
public:
    Server(int port, KVStore& store);
    ~Server();
    void run();

private:
    int     port_;
    int     listen_fd_;
    int     epfd_;
    KVStore& store_;
    std::unordered_map<int, Connection> conns_;

    void acceptConnection();
    void handleEvent(epoll_event& ev);
    void readFrom(Connection& conn);
    void writeTo(Connection& conn);
    void closeConn(int fd);
    std::string execute(const std::vector<std::string>& args);
    void setNonBlocking(int fd);
};
