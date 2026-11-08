#pragma once
// Ring buffer satu-producer/satu-consumer, aman lintas core (std::atomic).
#include <Arduino.h>
#include <atomic>

struct Ring {
  uint8_t* buf = nullptr;
  uint32_t size = 0;
  std::atomic<uint32_t> head{0};
  std::atomic<uint32_t> tail{0};

  bool init(uint32_t sz, bool usePsram) {
    buf = usePsram ? (uint8_t*)ps_malloc(sz) : (uint8_t*)malloc(sz);
    size = buf ? sz : 0;
    return buf != nullptr;
  }
  inline uint32_t avail() const {
    uint32_t h = head.load(std::memory_order_acquire);
    uint32_t t = tail.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : (size - t + h);
  }
  inline uint32_t freeSpace() const { return size - 1 - avail(); }

  // HANYA dipanggil oleh producer
  uint32_t write(const uint8_t* src, uint32_t len) {
    uint32_t fr = freeSpace();
    if (len > fr) len = fr;
    if (!len) return 0;
    uint32_t h = head.load(std::memory_order_relaxed);
    uint32_t first = size - h;
    if (len <= first) memcpy(buf + h, src, len);
    else { memcpy(buf + h, src, first); memcpy(buf, src + first, len - first); }
    head.store((h + len) % size, std::memory_order_release);
    return len;
  }
  // HANYA dipanggil oleh consumer
  uint32_t read(uint8_t* dst, uint32_t len) {
    uint32_t av = avail();
    if (len > av) len = av;
    if (!len) return 0;
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint32_t first = size - t;
    if (len <= first) memcpy(dst, buf + t, len);
    else { memcpy(dst, buf + t, first); memcpy(dst + first, buf, len - first); }
    tail.store((t + len) % size, std::memory_order_release);
    return len;
  }
  // hanya saat producer & consumer dijamin diam
  void reset() { head.store(0); tail.store(0); }
};
