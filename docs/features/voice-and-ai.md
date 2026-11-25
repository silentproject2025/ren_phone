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

**Choosing a model (AI_MODEL_PICK_V1).** Tap the title at the top left (*AI Chat*, the status dot and the model name, with a small arrow) to open a list of models: 3.5 Flash-Lite (default), 3.5 Flash, 3.6 Flash, 3.7 Flash and 2.5 Flash. *Custom...* opens the keyboard so you can type any model ID your API key can use (for example `gemini-2.5-pro`); tap above the keyboard to save it. The choice is kept in NVS and survives a restart and *New*. It can't be changed while an answer is loading. Chess vs. AI keeps its own fixed model. Model names change often, so if an ID stops working, the error shows up as an `HTTP Error 404` message in the chat; pick another model or type a current ID.

**Screen layout (CHAT_UI_V2).** AI Chat is shown as a conversation: your questions are bubbles on the right, Gemini's answers bubbles on the left. The last 6 exchanges stay on screen while the phone is on (they are kept in PSRAM and are not saved; the Gemini memory on the SD card works as before). While Gemini works, a bubble with bouncing dots shows. A new answer opens at its first line; a down-arrow button jumps to the newest message. On an empty chat three suggestions can be tapped to send at once. *New* (was *Mem*) clears the SD memory and the on-screen history; the *API Key* pill shows a green or red dot and opens the key editor.

## Voice Memo

Records through the INMP441 microphone into a WAV file in `/memo`, which is then played through the Music BT path.
