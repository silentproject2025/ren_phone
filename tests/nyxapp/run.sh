#!/usr/bin/env bash
# Jalankan SEMUA tes Nyx Apps di PC (butuh g++ & gcc; tanpa hardware).
#   cd tests/nyxapp && ./run.sh
# Mencakup: arena/MemMgr (fuzz + sanitizer), host native (manifest/sandbox/izin/HTTP/TLS gate),
# app contoh C dijalankan lewat dlopen, dan cek-sintaks glue firmware (.ino) dengan stub.
set -euo pipefail
cd "$(dirname "$0")"
FW=../../firmware/phone
OUT=${TMPDIR:-/tmp}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

echo "== 1/5 app contoh -> .so (gcc -Wall -Wextra, C11)"
for a in hello bounce memhog notes leaky; do
  gcc -std=c11 -Wall -Wextra -Werror -O2 -shared -fPIC -I $FW ../../examples/native/$a/app.c -o $OUT/$a.so
done

echo "== 2/5 nyxmem (arena, quota, tekanan, TLS gate)"
g++ -std=c++17 -O1 -g -Wall -Wextra $SAN -I $FW test_mem.cpp -o $OUT/test_mem && $OUT/test_mem

echo "== 3/5 host native + app contoh"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-result $SAN -I $FW test_host.cpp -ldl -o $OUT/test_host && $OUT/test_host

echo "== 4/5 nyxstore_core (indeks katalog Nyx Store, validasi, status pasang; fuzz + sanitizer)"
g++ -std=c++17 -O1 -g -Wall -Wextra $SAN -I $FW test_store.cpp -o $OUT/test_store && $OUT/test_store

echo "== 5/5 cek-sintaks glue firmware (stub; tanpa & dengan ELF loader)"
mkdir -p $OUT/nyx_elfstub
cat > $OUT/nyx_elfstub/esp_elf.h <<'EOH'
#pragma once
typedef struct { int x[8]; } esp_elf_t;
inline int esp_elf_init(esp_elf_t*) { return 0; }
inline int esp_elf_relocate(esp_elf_t*, const unsigned char*, unsigned long) { return 0; }
inline int esp_elf_request(esp_elf_t*, int, int, char**) { return 0; }
inline void esp_elf_deinit(esp_elf_t*) {}
EOH
# Arduino menggabungkan .ino berurutan nama (nyxapp_ lalu nyxstore_): cek-sintaks yang sama
cat $FW/nyxapp_renphone.ino $FW/nyxstore_renphone.ino > $OUT/nyx_glue_all.cpp
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -fsyntax-only -include stub/arduino_stub.h -I $FW -I stub -x c++ $OUT/nyx_glue_all.cpp
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -fsyntax-only -include stub/arduino_stub.h -I $FW -I stub -I $OUT/nyx_elfstub -x c++ $OUT/nyx_glue_all.cpp
echo "SEMUA LULUS"
