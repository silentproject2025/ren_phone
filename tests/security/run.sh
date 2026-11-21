#!/usr/bin/env bash
# Tes PC keamanan:
#   1) sec_core.h  (hash sandi, API key terenkripsi, penguncian, info.txt bertanda tangan) + silang-cek Python
#   2) blok login web ASLI dari phone.ino dikompilasi dgn mock Arduino: login, cache, penguncian bertingkat,
#      halaman /passwd, migrasi sandi lama, simulasi reboot
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wall -Wextra -fsanitize=address,undefined -o /tmp/test_sec test_sec.cpp
/tmp/test_sec
/tmp/test_sec --vec > /tmp/sec_vec.txt
python3 crosscheck.py /tmp/sec_vec.txt

python3 extract_web_blocks.py ../../firmware/phone/phone.ino /tmp/extracted_blocks.inc
cp /tmp/extracted_blocks.inc ./extracted_blocks.inc
g++ -std=c++17 -I../../firmware/phone -O1 -Wall -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -o /tmp/web_harness web_auth_harness.cpp
for m in a b migrate legacy-bad keys; do /tmp/web_harness "$m" | grep -v '^  \[serial\]'; done
rm -f ./extracted_blocks.inc
