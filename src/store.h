#pragma once
#include <string>
#include <unordered_map>
#include <list>
#include <optional>
#include <mutex>
#include <chrono>

class KVStore {
public:
    explicit KVStore(size_t max_keys = 100000);

    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value, int ttl_sec = -1);
    int  del(const std::string& key);
    bool expire(const std::string& key, int seconds);
    int  ttl(const std::string& key);   // -2=missing, -1=no expiry, else seconds left
    bool exists(const std::string& key);
    void purgeExpired();                 // called by background reaper
    bool snapshot(const std::string& path);
    bool restore(const std::string& path);

private:
    using Clock = std::chrono::steady_clock;
    using TP    = Clock::time_point;

    struct Entry {
        std::string value;
        TP          expiry{};
        bool        has_expiry = false;
        std::list<std::string>::iterator lru_it;
    };

    std::mutex mu_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> lru_;    // front = most recently used
    size_t max_keys_;

    bool isExpired(const Entry& e) const;
    void touch(const std::string& key);
    void evictOne();
};
