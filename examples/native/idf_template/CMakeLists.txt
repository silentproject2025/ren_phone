# Template proyek ESP-IDF untuk membangun SATU app native NyxOS menjadi ELF.
#   export NYX_SDK=/path/ke/repo/firmware/phone      # tempat nyx_app.h
#   cp ../hello/app.c main/app.c                      # atau app milikmu
#   idf.py set-target esp32s3
#   idf.py elf                                        # -> build/nyxapp.app.elf
# Lalu: python3 ../../../tools/pack_app.py --id hello --elf build/nyxapp.app.elf \
#         --manifest ../hello/manifest.txt --sd /media/SDCARD
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nyxapp)

# dari espressif/elf_loader (didaftarkan di main/idf_component.yml)
include(elf_loader)
project_elf(nyxapp)
