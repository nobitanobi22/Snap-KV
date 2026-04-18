#include "server.h"
#include "store.h"
#include "reaper.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    int port = 6399;  // default — avoids conflict with real Redis on 6379
    if (argc > 1) port = std::stoi(argv[1]);

    KVStore store;

    // Load last snapshot if it exists
    if (store.restore("snapkv.rdb"))
        std::cout << "[restore] loaded snapkv.rdb\n";

    // Background reaper: active TTL sweep every second
    Reaper reaper(store, 1);
    reaper.start();

    // Background snapshot: persist to disk every 60 seconds
    std::thread snapThread([&store] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            if (store.snapshot("snapkv.rdb"))
                std::cout << "[snapshot] saved to snapkv.rdb\n";
        }
    });
    snapThread.detach();

    try {
        Server server(port, store);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
