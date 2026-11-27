#!/bin/sh
# Jalankan dari folder tests/devpins:  ./run.sh   (tes 3 profil papan)
set -e
cd "$(dirname "$0")"
INC=../../firmware/phone
for flags in "" "-DRP_DISPLAY_ST7796" "-DRP_BOARD_AETHER"; do
  g++ -std=c++17 -Wall -Wextra -I$INC $flags -o /tmp/test_devpins test_devpins.cpp
  /tmp/test_devpins
done
