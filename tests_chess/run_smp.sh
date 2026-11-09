#!/bin/sh
# Uji core ke-2 (Lazy SMP) di PC: mesin utama (smp_test.cpp) + pembantu (chess_smp.cpp) = dua translation unit terpisah, persis seperti di ESP32.
cd "$(dirname "$0")" && ./extract.sh && cp engine_ai.inc chess_engine.inc && \
g++ -O2 -std=c++11 -pthread -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -I. -o /tmp/smp_test smp_test.cpp ../chess_smp.cpp -lm && /tmp/smp_test
