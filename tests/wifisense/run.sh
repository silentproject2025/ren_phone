#!/bin/sh
# Uji host untuk logika WiFi Sensing (tanpa ESP32): sinyal sintetis -> gerak / napas.
# Memakai $TMPDIR (cadangan /tmp) supaya juga jalan di Termux, yang tidak punya /tmp.
T="${TMPDIR:-/tmp}"
cd "$(dirname "$0")" && g++ -std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -Istub -o "$T/test_wsense" test_wsense.cpp && "$T/test_wsense"
