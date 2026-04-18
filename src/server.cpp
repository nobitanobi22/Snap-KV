#include "server.h"
#include "parser.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>

static const int MAX_EVENTS = 256;
static const int READ_SIZE  = 4096;

Server::Server(int port, KVStore& store) : port_(port), store_(store) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket failed");

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("bind failed — port in use?");
    if (listen(listen_fd_, SOMAXCONN) < 0)
        throw std::runtime_error("listen failed");

    epfd_ = epoll_create1(0);
    if (epfd_ < 0) throw std::runtime_error("epoll_create1 failed");

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd_;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    std::cout << "SnapKV listening on :" << port_ << "\n";
    std::cout << "Connect with: redis-cli -p " << port_ << "\n";
}

Server::~Server() {
    close(listen_fd_);
    close(epfd_);
}

void Server::setNonBlocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

void Server::acceptConnection() {
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) break;  // EAGAIN — no more pending connections
        setNonBlocking(fd);
        conns_.emplace(fd, Connection(fd));
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
    }
}

void Server::closeConn(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    conns_.erase(fd);
}

void Server::readFrom(Connection& conn) {
    int fd = conn.fd;
    char buf[READ_SIZE];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeConn(fd); return;
        }
        if (n == 0) { closeConn(fd); return; }
        conn.read_buf.append(buf, n);
    }
    // Parse as many complete commands as possible from the buffer
    while (!conn.read_buf.empty()) {
        auto res = parseRESP(conn.read_buf);
        if (res.status == ParseStatus::INCOMPLETE) break;
        if (res.status == ParseStatus::ERROR) {
            conn.write_buf += "-ERR protocol error\r\n";
            conn.read_buf.clear();
            break;
        }
        conn.read_buf.erase(0, res.consumed);
        conn.write_buf += execute(res.args);
    }
    // If we have responses to send, register EPOLLOUT
    if (!conn.write_buf.empty()) {
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLOUT;
        ev.data.fd = fd;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void Server::writeTo(Connection& conn) {
    int fd = conn.fd;
    while (!conn.write_buf.empty()) {
        ssize_t n = write(fd, conn.write_buf.data(), conn.write_buf.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeConn(fd); return;
        }
        conn.write_buf.erase(0, n);
    }
    // All flushed — stop watching for EPOLLOUT
    if (conn.write_buf.empty()) {
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void Server::handleEvent(epoll_event& ev) {
    int fd = ev.data.fd;
    if (fd == listen_fd_) { acceptConnection(); return; }

    auto it = conns_.find(fd);
    if (it == conns_.end()) return;

    if (ev.events & (EPOLLERR | EPOLLHUP)) { closeConn(fd); return; }
    if (ev.events & EPOLLIN)  readFrom(it->second);
    // Re-find: readFrom may have closed the connection
    if (ev.events & EPOLLOUT) {
        auto it2 = conns_.find(fd);
        if (it2 != conns_.end()) writeTo(it2->second);
    }
}

void Server::run() {
    epoll_event events[MAX_EVENTS];
    while (true) {
        int n = epoll_wait(epfd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) handleEvent(events[i]);
    }
}

// ---- Command execution ----
std::string Server::execute(const std::vector<std::string>& args) {
    if (args.empty()) return "-ERR empty command\r\n";
    std::string cmd = toUpper(args[0]);

    if (cmd == "PING") {
        if (args.size() > 1)
            return "$" + std::to_string(args[1].size()) + "\r\n" + args[1] + "\r\n";
        return "+PONG\r\n";
    }
    if (cmd == "SET") {
        if (args.size() < 3) return "-ERR wrong number of args\r\n";
        int ttl = -1;
        if (args.size() >= 5 && toUpper(args[3]) == "EX") {
            try { ttl = std::stoi(args[4]); } catch (...) { return "-ERR invalid expire\r\n"; }
        }
        store_.set(args[1], args[2], ttl);
        return "+OK\r\n";
    }
    if (cmd == "GET") {
        if (args.size() < 2) return "-ERR wrong number of args\r\n";
        auto val = store_.get(args[1]);
        if (!val) return "$-1\r\n";
        return "$" + std::to_string(val->size()) + "\r\n" + *val + "\r\n";
    }
    if (cmd == "DEL") {
        if (args.size() < 2) return "-ERR wrong number of args\r\n";
        int count = 0;
        for (size_t i = 1; i < args.size(); i++) count += store_.del(args[i]);
        return ":" + std::to_string(count) + "\r\n";
    }
    if (cmd == "EXPIRE") {
        if (args.size() < 3) return "-ERR wrong number of args\r\n";
        int sec = 0;
        try { sec = std::stoi(args[2]); } catch (...) { return "-ERR invalid\r\n"; }
        return store_.expire(args[1], sec) ? ":1\r\n" : ":0\r\n";
    }
    if (cmd == "TTL") {
        if (args.size() < 2) return "-ERR wrong number of args\r\n";
        return ":" + std::to_string(store_.ttl(args[1])) + "\r\n";
    }
    if (cmd == "EXISTS") {
        if (args.size() < 2) return "-ERR wrong number of args\r\n";
        return store_.exists(args[1]) ? ":1\r\n" : ":0\r\n";
    }
    if (cmd == "QUIT") return "+OK\r\n";
    return "-ERR unknown command '" + args[0] + "'\r\n";
}
