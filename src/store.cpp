#include "store.h"
#include <fstream>

KVStore::KVStore(size_t max_keys) : max_keys_(max_keys) {}

bool KVStore::isExpired(const Entry& e) const {
    return e.has_expiry && Clock::now() >= e.expiry;
}

// Move key to front of LRU list (O(1) since we hold the iterator)
void KVStore::touch(const std::string& key) {
    auto& e = map_[key];
    lru_.erase(e.lru_it);
    lru_.push_front(key);
    e.lru_it = lru_.begin();
}

// Evict least recently used key
void KVStore::evictOne() {
    if (lru_.empty()) return;
    map_.erase(lru_.back());
    lru_.pop_back();
}

std::optional<std::string> KVStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    if (isExpired(it->second)) {
        lru_.erase(it->second.lru_it);
        map_.erase(it);
        return std::nullopt;
    }
    touch(key);
    return it->second.value;
}

void KVStore::set(const std::string& key, const std::string& value, int ttl_sec) {
    std::lock_guard<std::mutex> lock(mu_);
    // Remove old entry if exists
    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_.erase(it->second.lru_it);
        map_.erase(it);
    } else if (map_.size() >= max_keys_) {
        evictOne();
    }
    lru_.push_front(key);
    Entry e;
    e.value  = value;
    e.lru_it = lru_.begin();
    if (ttl_sec > 0) {
        e.has_expiry = true;
        e.expiry     = Clock::now() + std::chrono::seconds(ttl_sec);
    }
    map_[key] = std::move(e);
}

int KVStore::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return 0;
    lru_.erase(it->second.lru_it);
    map_.erase(it);
    return 1;
}

bool KVStore::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end() || isExpired(it->second)) return false;
    it->second.has_expiry = true;
    it->second.expiry     = Clock::now() + std::chrono::seconds(seconds);
    return true;
}

int KVStore::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return -2;
    if (!it->second.has_expiry) return -1;
    if (isExpired(it->second)) {
        lru_.erase(it->second.lru_it);
        map_.erase(it);
        return -2;
    }
    return (int)std::chrono::duration_cast<std::chrono::seconds>(
        it->second.expiry - Clock::now()).count();
}

bool KVStore::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    if (isExpired(it->second)) {
        lru_.erase(it->second.lru_it);
        map_.erase(it);
        return false;
    }
    return true;
}

// Active expiry sweep — called by reaper thread every second
void KVStore::purgeExpired() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = map_.begin(); it != map_.end(); ) {
        if (isExpired(it->second)) {
            lru_.erase(it->second.lru_it);
            it = map_.erase(it);
        } else {
            ++it;
        }
    }
}

// Binary snapshot: [key_len(4)][key][val_len(4)][val][has_expiry(1)][ttl_ms(8)?]
bool KVStore::snapshot(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    for (auto& [k, e] : map_) {
        if (isExpired(e)) continue;
        uint32_t klen = k.size(), vlen = e.value.size();
        f.write((char*)&klen, 4);
        f.write(k.data(), klen);
        f.write((char*)&vlen, 4);
        f.write(e.value.data(), vlen);
        f.write((char*)&e.has_expiry, 1);
        if (e.has_expiry) {
            int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                e.expiry - Clock::now()).count();
            f.write((char*)&ms, 8);
        }
    }
    return true;
}

bool KVStore::restore(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear(); lru_.clear();
    while (f.peek() != EOF) {
        uint32_t klen, vlen;
        f.read((char*)&klen, 4); if (!f) break;
        std::string key(klen, 0); f.read(key.data(), klen);
        f.read((char*)&vlen, 4);
        std::string val(vlen, 0); f.read(val.data(), vlen);
        bool has_exp; f.read((char*)&has_exp, 1);
        int64_t ms = 0;
        if (has_exp) f.read((char*)&ms, 8);
        if (has_exp && ms <= 0) continue;   // already expired
        lru_.push_back(key);
        Entry e;
        e.value   = val;
        e.lru_it  = std::prev(lru_.end());
        if (has_exp) {
            e.has_expiry = true;
            e.expiry     = Clock::now() + std::chrono::milliseconds(ms);
        }
        map_[key] = std::move(e);
    }
    return true;
}
