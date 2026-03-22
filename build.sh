#!/bin/bash
set -e
mkdir -p build
cd build
cmake ..
cmake --build . -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
echo ""
echo "Build complete. Run: ./build/vrr_flicker_test"
