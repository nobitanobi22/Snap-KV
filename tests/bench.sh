#!/bin/bash
# Requires: redis-benchmark (sudo apt install redis-tools)
echo "Benchmarking SnapKV on port 6399..."
redis-benchmark -p 6399 -n 100000 -c 50 -q
