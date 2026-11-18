# Remote Control and web gamepad `OK`

The device can be controlled from another phone or laptop over WiFi, with no app to install.

- [Remote Control](#remote-control)
- [Web gamepad](#web-gamepad)

## Remote Control

The device broadcasts **its own WiFi network (Access Point)**. Connect another phone or laptop to it, open `http://192.168.4.1/remote`, and **the entire Accretion interface appears in the browser and can be controlled**.

| Capability | How it works |
|---|---|
| Live screen | The canvas is sent as **16×16 tiles that changed** (delta), so a still interface uses almost no data. *Saver* mode uses RGB332 (half the size). |
| Touch | Tap, drag and hold in the browser are injected as real touches, so the **whole** interface responds as if touched directly. |
| Buttons | Back, Home, Recents, Notifications, Control Center, Lock, Screenshot, a brightness slider, the app list, and text typing from the controlling device's keyboard. |
| Settings | AP on/off, WiFi name and password, auto-start, view-only mode, keep screen on, image quality, FPS limit, permission over home WiFi, and **No preview**. Can be set from the web **or** from the *Remote* app on the device. |
| No preview | Turns off sending the screen to the web (only a small header is sent, with no buffer or snapshot allocation). Touch is still forwarded. Useful for measuring real FPS, for example in the Camera app, without streaming overhead. |

**Security.** The AP uses WPA2 with a password generated the first time it is used (shown in the Remote app and changeable). The endpoint only serves connections through the AP; access through home WiFi is refused unless the *Allow via home WiFi* option is turned on. Only one viewer is active at a time. See also [Security](../security.md).

**Good to know.** Enabling the AP consumes internal RAM. If HTTPS features (Gemini, NASA, Trivia) suddenly produce `HTTP -1`, turn the AP off first. Screens drawn directly to the panel (NES, DOOM, the video player) do not pass through the canvas, so in the browser they appear frozen with a marker, but touch is still forwarded.

## Web gamepad

Another device acts as a **controller** for NES and DOOM. Connect it to the Accretion WiFi, open `http://192.168.4.1/pad`, then start the game.

- **Transport:** WebSocket on port 81 (the `WebSockets` library). The browser sends a 1-byte *bitmask* each time the button state changes.
- **Full DOOM:** strafe, run, weapons 1–7, MAP, keys F1–F11, mouse-style drag-to-look, and text entry (save names, cheats).
- **Laptop/PC:** the `/pad` page can be driven with keyboard and mouse (WASD or arrows, Space, Enter, Esc, 1–7; the `L` key toggles DOOM *mouse-look*).
- **HW Monitor HUD:** a transparent *overlay* with per-core CPU, RAM/PSRAM, temperature, battery and game FPS.
- **Stuck-input guard:** if the client disconnects, is silent for more than 700 ms, or the game changes, all buttons are reset to 0 so they do not get "stuck". At most 5 clients; their inputs are merged.
