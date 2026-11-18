#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <vector>
struct HardwareSerial { virtual ~HardwareSerial(){} std::vector<uint8_t> out; size_t write(const uint8_t* b, size_t n){ out.insert(out.end(), b, b+n); return n; } size_t write(uint8_t b){ out.push_back(b); return 1; } };
