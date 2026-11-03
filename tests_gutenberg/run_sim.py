#!/usr/bin/env python3
# Simulasi alur unduh/cek/verifikasi Gutenberg di PC (tanpa ESP32): mengambil kode ASLI dari ../gutenberg_renphone.ino,
# mengompilasinya dgn stub Arduino (SD = folder biasa) dan menjalankan test_gutdl_sim.cpp.
#   python3 run_sim.py          (butuh g++ >= C++11)
import re, subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'gutenberg_renphone.ino'), encoding='utf-8').read()
def cut(a, b):
    i = src.index(a); j = src.index(b, i); return src[i:j]
open(os.path.join(here, 'sim_globals.inc'), 'w', encoding='utf-8').write(cut('#define GUT_CAP', 'static StackType_t* gutStack'))
open(os.path.join(here, 'sim_block.inc'), 'w', encoding='utf-8').write(cut('static void gutSetErr(int rc) {', 'static void gutTaskFunc(void*) {'))
out = os.path.join(here, 'sim')
r = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-o', out, os.path.join(here, 'test_gutdl_sim.cpp')])
if r.returncode: sys.exit(r.returncode)
sys.exit(subprocess.run([out]).returncode)
