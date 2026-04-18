# SnapKV

A Redis-compatible in-memory key-value store built from scratch in C++.

## Architecture

```
Client (redis-cli / any Redis client)
         |  RESP protocol over TCP
         v
    [ Server ]  ← epoll event loop (single thread, non-blocking I/O)
         |
    [ Parser ]  ← parses RESP wire protocol from raw bytes
         |
    [ Store  ]  ← hash map + LRU list + TTL expiry
         |
    [ Reaper ]  ← background thread, active TTL sweep every 1s
    [ Snapshot] ← background thread, binary dump to disk every 60s
```

## Files

| File | What it does |
|---|---|
| `server.cpp` | epoll loop — accepts connections, reads/writes non-blocking |
| `store.cpp` | hash map + doubly-linked LRU + TTL + snapshot |
| `parser.cpp` | RESP protocol parser (multi-bulk + inline) |
| `connection.h` | per-client read/write buffer state |
| `reaper.cpp` | background thread for active key expiry |
| `main.cpp` | wires everything, starts background threads |

## Commands

```
PING
SET key value
SET key value EX seconds
GET key
DEL key [key ...]
EXPIRE key seconds
TTL key
EXISTS key
```

## Build & Run

```bash
make
./snapkv          # runs on port 6399
./snapkv 7000     # custom port
```

## Connect

```bash
redis-cli -p 6399
```

## Benchmark Results

100k requests, 50 concurrent clients (`redis-benchmark -p 6399 -n 100000 -c 50 -q`):

```
PING:   217,391 req/sec   p50=0.111ms
SET:    180,505 req/sec   p50=0.119ms
GET:    246,913 req/sec   p50=0.103ms
```

To reproduce:
```bash
apk add redis       # Alpine/WSL
redis-benchmark -p 6399 -n 100000 -c 50 -q
```
