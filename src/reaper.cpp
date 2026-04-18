#include "reaper.h"
#include <chrono>

Reaper::Reaper(KVStore& store, int interval_sec)
    : store_(store), interval_sec_(interval_sec) {}

Reaper::~Reaper() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Reaper::start() {
    thread_ = std::thread([this] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(interval_sec_));
            store_.purgeExpired();
        }
    });
}
