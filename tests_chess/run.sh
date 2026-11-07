#!/bin/sh
cd "$(dirname "$0")" && ./extract.sh && g++ -O2 -std=c++11 -Wall -Wno-unused-function -o /tmp/test_chess test_chess.cpp -lm && /tmp/test_chess
