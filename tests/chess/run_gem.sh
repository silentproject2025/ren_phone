#!/bin/sh
cd "$(dirname "$0")" && ./extract.sh && g++ -O2 -std=c++11 -Wall -Wno-unused-function -Wno-unused-variable -o /tmp/test_gem test_gem.cpp -lm && /tmp/test_gem
