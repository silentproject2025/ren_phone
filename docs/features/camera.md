# Camera (OV2640 on the ESP32-CAM) `OK`

The camera sits on the **ESP32-CAM** module (the coprocessor), not on the S3. JPEG images travel over the same UART link as Bluetooth audio (`rplink.h`). **Both firmwares must be reflashed:** the S3 (`firmware/phone`) and the ESP32-CAM (`firmware/cam_bt_coprocessor`). Without the new CAM firmware, the app shows "CAM does not support the camera yet" and other features are unaffected.

Relevant files: `camera_renphone.ino`, `cam_rx.h`, `cam_avi.h` (S3 side) and `cam_module.h`, `cam_tx.h` (CAM side). No extra library is required (the camera driver is part of the ESP32 core).

> **Status.** All Camera features were tested on a device by the owner, including the latest link optimisations (ACK without waiting for the UI, JPEG header dedup, UART up to 5 Mbaud), which the owner reports gave a significant improvement as expected. Before device testing, the syntax was checked with a *mock API*. The protocol (frame splitting and reassembly, corrupted frames, slots, still vs. preview), the AVI writer, header dedup, sequence-number flow control (including 256 wrap-around, stale or bogus ACKs, rescue), stale header ids, a lost cache with recovery, and bit-exact corrupted-frame handling were tested on a PC (`tests/camera/test_cam.cpp`); the generated AVI was decoded by ffmpeg (10 frames, 10 fps, valid `idx1`). The **GC2145 software-JPEG path was not compiled with the ESP32 toolchain or run on a device** (logic tested with a mock on a PC).

- [Features](#features)
- [Automatic sensor detection](#automatic-sensor-detection)
- [GC2145 sensors (no hardware JPEG)](#gc2145-sensors-no-hardware-jpeg)
- [Performance work](#performance-work)
- [Force 5 Mbaud](#force-5-mbaud-link_force_v1)
- [Risks to be aware of](#risks-to-be-aware-of)

## Features

| Feature | Details |
|---|---|
| Live preview | 160×120 (2×), 320×240 and 640×480 (1/2 decode). The screen is forced to landscape. The HUD shows resolution, FPS, KB/frame and a fast-link indicator. |
| Photo | Up to 1600×1200, saved **as-is from the sensor** to `/Kamera/IMG_*.jpg` (no re-encoding); appears automatically as the "Kamera" album in the Gallery. 3/10 s timer, burst 3×/5×, JPEG quality 4–30. |
| LED flash (GPIO 4, PWM) | Torch at 30% or 100%, or a full flash when shooting. The torch turns off automatically after 2 minutes. |
| Video | MJPEG AVI at `/Kamera/VID_*.avi`, playable by the built-in AVI player and by VLC on a PC. The *Play* button plays the latest video immediately. |
| Timelapse | 2/5/10/30/60 second interval into one AVI file (played at 10 fps). |
| Motion detection | 1/8-scale JPEG decode (DC component only) with global light compensation. Saves `GERAK_*.jpg` automatically, with vibration and a *toast*, and a 6-second pause. |
| Sensor controls | Brightness, contrast, saturation, sharpness, white balance, effects (negative/grey/sepia/RGB), exposure, night mode, lens correction, mirror, flip, guide lines and preview quality. |

## Automatic sensor detection

When the camera is switched on, the driver identifies the sensor over the SCCB bus and the CAM reports it to the S3 (a toast such as "OV3660 - max photo 2048x1536"; also shown in Set → System).

| Sensor | Behaviour |
|---|---|
| OV2640 | Photos up to 1600×1200. |
| OV3660 / OV5640 | Photos up to 2048×1536 (the S3 photo buffer is enlarged automatically). OV3660 is corrected automatically (built-in vflip, initial saturation and brightness) so it is not upside-down or oversaturated; the Mirror/Flip buttons still work relative to that correction. OV5640 autofocus is not enabled. |
| Not detected | A clear message is shown (check the flat cable and seating). |
| No JPEG output (for example OV7670/OV7725) | A clear message is shown. |

What is not automatic: the pin map of other boards. The code uses AI-Thinker pins; clone boards with different pins need `CAM_PIN_*` changed in `cam_module.h`. The camera uses the standard AI-Thinker pins and the UART link (GPIO 13/14) does not clash; the SD card slot on the ESP32-CAM must stay empty (GPIO 4 is the flash LED).

## GC2145 sensors (no hardware JPEG)

A camera module labelled **RHYX M21-45** on some ESP32-CAM boards is a **GC2145** sensor: it only outputs raw RGB565 and has **no hardware JPEG** (marker `GC2145_SOFTJPEG_V1`). The CAM first tries JPEG; if the driver answers "sensor without JPEG", the camera is restarted in RGB565 mode (SVGA, 2 PSRAM buffers of about 1.9 MB) and every frame is JPEG-encoded on the CAM (`frame2jpg`). The UART link, header dedup, S3, Gallery and AVI are unchanged.

- Detected automatically: a toast "GC2145 - max photo 800x600"; in the CAM serial log: `[CAM] kamera nyala PID=0x2145 ... [RGB565+JPEG software]`.
- **Maximum photo 800×600** (the limit of the RGB565 buffer in PSRAM). 320×240 preview is the most realistic; 640×480 is slower (software encoding).
- FPS is lower than the OV2640 (roughly 5–12 fps at 320×240): the sensor sends twice as much raw data and encoding happens on the CAM's CPU.
- Some sensor controls (night mode, lens correction, test pattern, and so on) may have no effect: functions the GC2145 driver does not implement are skipped (no crash).
- Odd colors (purple/green)? Add `-DCAM_SOFT_SWAP565` (or uncomment `//#define CAM_SOFT_SWAP565` in `cam_module.h`).
- Pins stay AI-Thinker. If the camera is not detected (error `0x105`), check the flat cable first.

## Performance work

| Area | Approach |
|---|---|
| UART link | Automatically rises from 921.6 kbaud to **2, 3, 4 or 5 Mbaud** when the app opens (the upper limit is chosen in Set → System; default 5M). Throughput is about 90 KB/s at 921k, 300 KB/s at 3M and about 500 KB/s at 5M. The 4M and 5M UART dividers are exact (80 MHz ÷ 20 and ÷ 16). It drops back when the app closes. |
| Automatic step-down | From the upper limit, the link steps down **one rung** (5M → 4M → 3M → 2M → 921k) if the cable is not clean: (a) at least 3 corrupted frames within 5 s, the CAM is asked to switch immediately; (b) no valid frame 1.5 s after a baud change, or the CAM missing for more than 2 s at a high baud, the S3 falls back to 921k, waits for the CAM to follow (3 s maximum), then tries the next rung down. It does not climb again in the same session. |
| ACK without waiting for the UI | The S3 sends `RP_CAM_ACK [seq]` as soon as the final chunk of a frame arrives (in `musTask`), not after the UI finishes decoding and drawing. The cable is not idle during decoding, while the UI still takes the latest frame. `camTask` is woken directly by the ACK (task notify) instead of polling every 2 ms. |
| Sequence-number flow control | A 3-frame window is computed from the difference between the send sequence number and the ACK, not a credit count. A lost ACK or frame does not leak credit or stall the stream. |
| JPEG header dedup | The OV2640 header (about 600–650 bytes: DQT+DHT+SOF) is identical in every preview frame, so it is removed and replaced by a 16-bit id; the S3 copies it from its cache. The id must match, otherwise the frame is dropped (it never shows wrong colors or a wrong picture). If the S3 cache is lost, a "needs header" bit in the ACK makes the next frame complete again; a full header is still sent every 30 frames. Most noticeable on small previews (about 25% saving at 160×120, about 8% at 320×240). |
| Frame flow | `GRAB_LATEST`: always the newest frame, low latency, and it does not choke music. |
| S3 side | CPU at 240 MHz while the app is open; UART read in 256-byte blocks (not per byte); RX buffer **10 KB** (up from 6 KB, +4 KB internal RAM; needed at 5 Mbaud); the link task spins every 1 tick while the camera is active. |
| CAM side | Camera buffers in PSRAM (2 frames, UXGA size); the camera is only powered while the app is open, so RAM is returned to BT/MP3 on exit; XCLK 10/16/20 MHz. |

Limits that were not changed: the CAM CPU is already 240 MHz; XCLK 20 MHz is already near the practical limit of the OV2640; the CAM PSRAM runs at 40 MHz (a core setting; changing it needs a custom core build).

## Force 5 Mbaud (`LINK_FORCE_V1`)

In Set → System → Link, the last option is **5 Mbaud FORCE**. Corrupted frames no longer step the link down (5M → 4M → ...); the upper limit stays at 5 Mbaud. If the link goes silent or the CAM disappears, the S3 still briefly falls back to 921k (so that the link can recover; the CAM also falls back by itself after 3 s) and then tries 5 Mbaud again about every 3.5 s; the upper limit is never lowered. The HUD shows "(FORCE)" and a warning toast (at most once per 8 s) when the link is not clean. Consequence: a bad cable means a choppy preview or corrupted frames, rather than a silent fall to a lower baud. The other options behave as usual.

## Risks to be aware of

1. **CAM internal RAM:** camera + A2DP + MP3 together is very tight. If the HUD or a message says "CAM RAM low", turn off Bluetooth or music first. The threshold is `CAM_MIN_BLOCK` (`cam_module.h`).
2. **Power:** the ESP32-CAM easily suffers a *brownout* with camera and BT radio together. Use the 5V pin with a strong supply.
3. **4–5 Mbaud UART cables** can be unstable if long; the link steps down one rung automatically, or choose a lower upper limit manually in Set → System.
4. **Flash both firmwares together.** The meaning of `RP_CAM_ACK` changed: a new CAM with an old S3 gives a choppy preview of about 1 fps (old ACKs are treated as stale), while an old CAM with a new S3 still works but without header dedup.
5. **S3 internal RAM:** the UART RX buffer rose from 6 KB to 10 KB (`CAM_UART_RXBUF`). If internal RAM is too tight, lower this value, at the cost of being less safe at 5 Mbaud.
6. Music may stutter briefly when the app opens or closes (the moment the baud rate changes).
7. The Gallery only recognises photos; AVI videos play through the *Play* button or the Files app.
8. The camera may be rotated on the board: use Mirror/Flip in Set → Display.
