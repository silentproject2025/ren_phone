# Media: music, video and gallery `OK`

- [Music (Bluetooth)](#music-bluetooth)
- [Online music (Jamendo)](#online-music-jamendo)
- [AVI / MJPEG player](#avi--mjpeg-player)
- [Gallery](#gallery)

The [Camera](./camera.md) and the [Books](./books.md) reader have their own pages.

## Music (Bluetooth)

Audio goes out through **Bluetooth TWS** earbuds, with the ESP32-CAM as the coprocessor (see [Architecture](../architecture.md#bluetooth-coprocessor-rplink)).

- Up to 200 songs in the `music/` folder.
- **Look.** Flat and single-accent: solid colour blocks, thin lines and square-ish corners, with no gradients, glow or glass. The cover placeholder is a flat geometric poster (Bauhaus-style shapes in solid colours) generated from the title.
- **Touch.** Drag the song list (and the Online list) with your finger, it glides after you lift it; tap a row to play (the song starts when you lift your finger). On the player, swipe left or right on the cover or title for the next or previous song; tap them to play or pause.
- **Album cover** from the ID3 tag (baseline JPEG only; other formats show a vinyl-record image).
- **Plain shuffle or AI mode** (RYNE v2, below).
- **Control from the earbuds' buttons** (AVRCP).
- If the CAM restarts, the reason is shown on the screen to help diagnosis.
- **Speaker output (optional).** With a MAX98357A module wired (see [Hardware](../hardware.md)), open the *Find TWS* page and tap **Output: BT / SPK** to switch. In SPK mode the S3 decodes MP3 (and voice-memo WAV) itself with the Helix decoder (`musspk.h`) and sends it over I2S; the CAM and earbuds are not needed. The choice is remembered. Only the music app and voice memos use the speaker: AVI video audio still goes through the CAM and Bluetooth. Phone calls follow the same setting (see [Phone calls](./phone-calls.md)). The decoder, its buffers and the task stack are placed in PSRAM and are freed when playback stops. With no SD card (or no MP3 on it) the app lists and plays MP3 files from internal FFat instead. This works with **both** outputs: through the speaker (the chip shows `SPK-F`) and through the Bluetooth TWS earbuds (the S3 streams the file to the CAM exactly like an SD track). FFat is read through a helper task with an internal-RAM stack, which costs roughly 12 KB of internal RAM only while a FFat track plays. While Output is SPK, the chip at the top right shows free internal RAM / largest block in KB and the Serial log prints `[SPK-RAM]` lines. This path has not been compiled or tested on hardware yet.

## Online music (Jamendo)

`BETA`: not yet compiled or tested on a device.

In the Music app, the **Online** chip next to the RYNE chip opens a page where you can search by title or artist, or browse popular tracks by tag. A **source chip** (*Jamendo* / *Audius*) next to the search box switches the catalogue; the choice is saved. **Partial titles work:** typing *ab* finds *ABCD*. The search matches anywhere in the track name (most popular first); if nothing matches it falls back to a normal search (artist names, tags, whole words). The list is then ordered so titles that start with what you typed come first, then titles with a word that starts with it, then the rest. Up to 40 results are shown; a longer query (*abc*) narrows the list. Tap a track to play it; when it ends, the next one in the list starts.

- **Audius** needs no key (public API, `app_name=ren_phone`). Search uses `/v1/tracks/search`; the genre chips show this week's trending tracks of Pop, Rock, Lo-Fi or Jazz (*Hits* = all genres). Tracks that are not streamable or are stream-gated are skipped. Streams are usually 320 kbps, so with *Output: BT* the ring holds about 3 seconds and a weak Wi-Fi may cause gaps. Audius is the default when the build has no Jamendo `client_id`.
- **Jamendo** needs Wi-Fi and a Jamendo `client_id`, set at build time with `JAM_CLIENT_ID` (the GitHub Secret of the same name in the official workflow). Forks without it get a placeholder and no results.
- **Output: BT (default).** A download task (`mbTaskFn` in `musicbt_renphone.ino`) fetches the MP3 over HTTP(S) into the 128 KB PSRAM ring (the same one the AVI player uses). The music task sends it to the ESP32-CAM over RPLink, and the CAM decodes it and plays it over A2DP to the TWS earbuds. Pause, volume, next/previous and the earbud buttons work as for SD tracks. The ID3 tag at the start of the stream is skipped. TWS must be connected first.
- **Output: SPK.** The S3 downloads and decodes the stream itself (`musspk.h`) and plays it through the MAX98357A.
- **Limits.** The buffer is about 8 seconds of audio, so a slow connection can cause gaps. A stream that stalls for 8 seconds is treated as ended. Opening an AVI video stops the online track (they share the ring). Online tracks are not part of RYNE learning and have no album cover. The stream is fetched over TLS without certificate verification, like Chat.

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
