#!/bin/sh
# Uji host untuk logika WiFi Sensing (tanpa ESP32): sinyal sintetis -> gerak / napas.
cd "$(dirname "$0")" && g++ -std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -Istub -o /tmp/test_wsense test_wsense.cpp && /tmp/test_wsense
