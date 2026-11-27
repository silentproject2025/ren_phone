#!/bin/sh
# Kompilasi + jalankan devopt_renphone.ino asli terhadap layar tiruan. Jalankan dari tests/devpins.
set -e
cd "$(dirname "$0")"
INC=../../firmware/phone
for flags in "" "-DRP_DISPLAY_ST7796" "-DRP_BOARD_AETHER"; do
  g++ -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable -Wno-format-security -Istub -I$INC $flags -o /tmp/test_devopt mock_devopt.cpp
  /tmp/test_devopt
done
