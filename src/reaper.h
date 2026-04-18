#pragma once
#include "store.h"
#include <thread>
#include <atomic>

// Runs a background thread that sweeps expired keys every N seconds.
// Complements lazy expiry (keys checked on GET) with active cleanup.
class Reaper {
public:
    Reaper(KVStore& store, int interval_sec = 1);
    ~Reaper();
    void start();

private:
    KVStore&          store_;
    int               interval_sec_;
    std::thread       thread_;
    std::atomic_bool  running_{true};
};
