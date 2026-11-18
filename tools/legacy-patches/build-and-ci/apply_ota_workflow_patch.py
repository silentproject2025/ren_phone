#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_ota_workflow_patch.py -- ubah .github/workflows/build-firmware.yml supaya:
#   * build jalan di SEMUA branch (bukan cuma main/master/remote-control)
#   * nama branch + RP_BASE_REV disuntik ke firmware (-DRP_BRANCH=...)
#   * branch selain remote-control DITOLAK kalau RP_BASE_REV-nya beda (satu basis kode)
#   * tiap build menerbitkan info.txt (branch, build, base, size, md5, sha)
#   * rilis build-N dari branch non-stabil dijadikan PRERELEASE (tidak mengganggu tombol versi lama)
#   * tiap branch punya rilis "channel-<branch>" yang selalu menunjuk build terbarunya
# Idempoten & semua-atau-tidak-sama-sekali, sama seperti patch lain di repo ini.
import sys, os
wf = '.github/workflows/build-firmware.yml'
if not os.path.exists(wf):
    print("ERROR: %s tidak ada" % wf); sys.exit(1)
w = open(wf, encoding='utf-8').read()
if 'RP_BASE_REV' in w:
    print("Workflow sudah dipatch, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

w = rep(w, '    branches: [ "main", "master", "remote-control" ]\n  pull_request:',
           '    branches: [ "**" ]\n  pull_request:', "trigger push")

step = '''      - name: Tentukan nama branch & cek kompatibilitas base
        env:
          RAW_BRANCH: ${{ github.head_ref || github.ref_name }}
        run: |
          RP_BRANCH="$(printf '%s' "$RAW_BRANCH" | tr -c 'A-Za-z0-9_-' '-')"
          MY_BASE="$(grep -E '^#define RP_BASE_REV[[:space:]]+[0-9]+' ota_check_renphone.ino | grep -oE '[0-9]+' | head -1)"
          echo "Branch=$RP_BRANCH  RP_BASE_REV=${MY_BASE:-?}"
          if [ -z "$MY_BASE" ]; then
            echo "::error::RP_BASE_REV tidak ditemukan di ota_check_renphone.ino"; exit 1
          fi
          if [ "$RP_BRANCH" != "remote-control" ]; then
            if git fetch --depth=1 origin remote-control 2>/dev/null; then
              REF_BASE="$(git show FETCH_HEAD:ota_check_renphone.ino 2>/dev/null | grep -E '^#define RP_BASE_REV[[:space:]]+[0-9]+' | grep -oE '[0-9]+' | head -1)"
              if [ -n "$REF_BASE" ] && [ "$REF_BASE" != "$MY_BASE" ]; then
                echo "::error::Basis kode branch '$RP_BRANCH' (RP_BASE_REV=$MY_BASE) BEDA dgn remote-control ($REF_BASE). Firmware-nya tidak boleh dipertukarkan -> build dibatalkan. Samakan RP_BASE_REV."
                exit 1
              fi
            fi
          fi
          echo "RP_BRANCH=$RP_BRANCH" >> "$GITHUB_ENV"
          echo "RP_BASE_REV=$MY_BASE" >> "$GITHUB_ENV"

      - name: Compile Sketch
'''
w = rep(w, "      - name: Compile Sketch\n", step, "compile step")

w = rep(w, "-DRP_BUILD_NUM=${{ github.run_number }} -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1",
           "-DRP_BUILD_NUM=${{ github.run_number }} -DRP_BRANCH=${RP_BRANCH} -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1",
        "compile flags")

w = rep(w, "          cp build/build_sketch.ino.merged.bin ren_phone_s3_merged.bin\n",
           "          cp build/build_sketch.ino.merged.bin ren_phone_s3_merged.bin\n"
           "          MD5=\"$(md5sum firmware.bin | cut -d' ' -f1)\"\n"
           "          SIZE=\"$(stat -c%s firmware.bin)\"\n"
           "          {\n"
           "            echo \"branch=${RP_BRANCH}\"\n"
           "            echo \"build=${{ github.run_number }}\"\n"
           "            echo \"base=${RP_BASE_REV}\"\n"
           "            echo \"size=${SIZE}\"\n"
           "            echo \"md5=${MD5}\"\n"
           "            echo \"sha=${{ github.sha }}\"\n"
           "          } > info.txt\n"
           "          cat info.txt\n",
        "siapkan firmware")

old = '''          TAG="build-${{ github.run_number }}"
          if gh release view "$TAG" >/dev/null 2>&1; then
            gh release upload "$TAG" firmware.bin ren_phone_s3_merged.bin --clobber
          else
            gh release create "$TAG" firmware.bin ren_phone_s3_merged.bin \\
              --target "${{ github.sha }}" \\
              --title "Firmware build ${{ github.run_number }}" \\
              --notes "Commit ${{ github.sha }} (${{ github.ref_name }})"
          fi
'''
new = '''          TAG="build-${{ github.run_number }}"
          # branch stabil = rilis biasa; branch lain = PRERELEASE (tidak dianggap "terbaru" oleh tombol versi lama)
          case "$RP_BRANCH" in main|master|remote-control) PRE="" ;; *) PRE="--prerelease" ;; esac
          if gh release view "$TAG" >/dev/null 2>&1; then
            gh release upload "$TAG" firmware.bin ren_phone_s3_merged.bin --clobber
          else
            gh release create "$TAG" firmware.bin ren_phone_s3_merged.bin $PRE \\
              --target "${{ github.sha }}" \\
              --title "Firmware build ${{ github.run_number }}" \\
              --notes "Commit ${{ github.sha }} (${{ github.ref_name }})"
          fi

          # channel bergerak per-branch -- dibaca fitur "pilih branch" di app Update (selalu prerelease)
          CH="channel-${RP_BRANCH}"
          NOTES="Channel branch ${RP_BRANCH}: build ${{ github.run_number }}, commit ${{ github.sha }}"
          if gh release view "$CH" >/dev/null 2>&1; then
            gh release upload "$CH" firmware.bin info.txt --clobber
            gh release edit "$CH" --prerelease --notes "$NOTES"
          else
            gh release create "$CH" firmware.bin info.txt --prerelease \\
              --target "${{ github.sha }}" \\
              --title "Channel ${RP_BRANCH}" \\
              --notes "$NOTES"
          fi
'''
w = rep(w, old, new, "publish release")
open(wf, 'w', encoding='utf-8').write(w)
print("OK: workflow dipatch")
