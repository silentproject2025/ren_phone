#!/usr/bin/env bash
# Tes PC sysmem.h (butuh g++; tanpa hardware):  cd tests/sysmem && ./run.sh
set -euo pipefail
cd "$(dirname "$0")"
FW=../../firmware/phone
OUT=${TMPDIR:-/tmp}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
g++ -std=c++17 -O1 -g -Wall -Wextra $SAN -I $FW test_sysmem.cpp -o $OUT/test_sysmem
$OUT/test_sysmem

echo "== cek-sintaks glue firmware (stub; -Wall -Wextra)"
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -fsyntax-only -include stub_fw.h -I $FW -x c++ $FW/sysmem_renphone.ino
echo "== patch phone.ino: anchor cocok + idempoten (pada salinan)"
T=$(mktemp -d); mkdir -p $T/firmware/phone; cp $FW/phone.ino $FW/sysmem.h $FW/sysmem_fw.h $FW/sysmem_renphone.ino $T/firmware/phone/; cp ../../tools/patches/apply_sysmem_patch.py $T/
(cd $T && python3 apply_sysmem_patch.py --check && python3 apply_sysmem_patch.py && python3 apply_sysmem_patch.py | grep -q "sudah terpasang" && grep -c SYSMEM_V1 firmware/phone/phone.ino)
rm -rf $T
echo "SEMUA LULUS"
