# Media: music, video and gallery `OK`

- [Music (Bluetooth)](#music-bluetooth)
- [AVI / MJPEG player](#avi--mjpeg-player)
- [Gallery](#gallery)

The [Camera](./camera.md) and the [Books](./books.md) reader have their own pages.

## Music (Bluetooth)

Audio goes out through **Bluetooth TWS** earbuds, with the ESP32-CAM as the coprocessor (see [Architecture](../architecture.md#bluetooth-coprocessor-rplink)).

- Up to 200 songs in the `music/` folder.
- **Album cover** from the ID3 tag (baseline JPEG only; other formats show a vinyl-record image).
- **Plain shuffle or AI mode** (RYNE v2, below).
- **Control from the earbuds' buttons** (AVRCP).
- If the CAM restarts, the reason is shown on the screen to help diagnosis.
- **Speaker output (optional).** With a MAX98357A module wired (see [Hardware](../hardware.md)), open the *Find TWS* page and tap **Output: BT / SPK** to switch. In SPK mode the S3 decodes MP3 (and voice-memo WAV) itself with the Helix decoder (`musspk.h`) and sends it over I2S; the CAM and earbuds are not needed. The choice is remembered. Only the music app and voice memos use the speaker: AVI video audio still goes through the CAM and Bluetooth. Phone calls follow the same setting (see [Phone calls](./phone-calls.md)). The decoder, its buffers and the task stack are placed in PSRAM and are freed when playback stops. With no SD card (or no MP3 on it) the app lists and plays MP3 files from internal FFat instead (SPK output only; the chip shows `SPK-F`). FFat is read through a helper task with an internal-RAM stack, which costs roughly 12 KB of internal RAM only while a FFat track plays. While Output is SPK, the chip at the top right shows free internal RAM / largest block in KB and the Serial log prints `[SPK-RAM]` lines. This path has not been compiled or tested on hardware yet.

**RYNE v2** is a song-recommendation engine that runs entirely on the device (`ryne_engine.h`, *header-only*). It estimates a song's "mood" (8 classes) from 13 behavioural and audio features using softmax and an HMM filter, then chooses a song with *contextual Thompson Sampling*. Its learning data is stored in `/ryne2.bin`.

## AVI / MJPEG player

Plays MJPEG video with MP3 audio in one `.avi` file; sound goes out through the TWS earbuds.

- **Synchronisation.** The master clock is the audio playback position on the CAM. Late frames are skipped so the sound never falls behind.
- **Video list** appears as cards with thumbnail, duration, size and a *Continue mm:ss* status. Thumbnails are stored on the SD card so later opens are instant.
- **Seek.** The `idx1` index is read once when the video opens, then a table of jump points is built about every 1 second. Seek accuracy is about ±1 second.
- **Resume.** The last position of 8 videos is remembered. When reopening, choose *Start* or *Continue*.
- **Automatic zoom.** Small videos are enlarged as much as possible with bilinear interpolation. The **Z** button turns it off if FPS drops.
- Without TWS, or if the audio is not MP3, the video still plays without sound.

| Gesture | Function |
|---|---|
| Tap | Opens the control panel and pauses. |
| Swipe right / left | Forward / back 10 seconds. |
| Swipe up/down, left half | Brightness. |
| Swipe up/down, right half | TWS volume. |
| Hold 0.6 s | Stop. |
| Swipe up from the bottom edge | Return to Home. |

**Creating a compatible video**

```bash
ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
       -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k output.avi
```

Audio **must be MP3 at 44.1 kHz**, preferably CBR so that seeking is accurate. For portrait video, use `scale=240:320`.

## Gallery

Albums are **folders on the SD card** (at most 2 levels), like a typical phone gallery. 72×72 thumbnails are generated once and stored in `/.thumbs/`, so the CPU does not need to decode the full image each time it opens. The cache name is a hash of the path, size and modification time, so a replaced image is regenerated automatically. Supported formats: **JPG and PNG**.
