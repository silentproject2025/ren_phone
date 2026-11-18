# Voice and AI

- [Wake word "Hei Nyx"](#wake-word-hei-nyx-ok)
- [AI Chat (Gemini)](#ai-chat-gemini)
- [Voice Memo](#voice-memo)

## Wake word "Hei Nyx" `OK`

The device listens continuously through the microphone. When the keyword is spoken, the device **vibrates, opens AI Chat and starts recording immediately**.

This feature is **not a neural network**. The user **records 4 samples** of the keyword themselves, and the device matches speech using classical techniques: MFCC (12 coefficients per frame) and DTW (tolerant of differences in speaking speed). All processing happens on the device without internet. The threshold is calibrated automatically and a *Sensitivity* slider is available.

The consequence is that the system recognises **the owner's own voice and pronunciation**, not just anyone saying the phrase, and accuracy drops in noisy rooms.

## AI Chat (Gemini)

Question and answer with Gemini over HTTPS, with text input or voice dictation. Conversation memory is stored on the SD card (`ai_memory.txt`). Requires `gemini_key.txt` (see [Configuration](../configuration.md)).

**Voice commands `BETA`.** Gemini can control the phone: *open app*, *timer*, *music* (play, pause, continue, back, shuffle), *brightness*, *Home* and *back*. At the end of its reply, Gemini writes an `@@CMD {json}` line (at most 3); that line is stripped from the displayed text and executed by the main loop. Examples (spoken in Indonesian or English): "Hei Nyx, set a timer for 10 minutes", "open Music and shuffle". Actions only run while the phone is unlocked, and only from the action list above (there is no deleting files or powering off).

## Voice Memo

Records through the INMP441 microphone into a WAV file in `/memo`, which is then played through the Music BT path.
