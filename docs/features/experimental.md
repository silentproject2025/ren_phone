# Experimental features and hardware notes

## SPI display overclock `HW`

**What it does.** An option in the **Settings** app that raises the SPI bus speed to the display. Two presets are available: **60 MHz** (safe, the default) and **80 MHz**.

**Why it exists.** Frames are drawn into a sprite and then pushed to the panel over SPI. A faster bus means the image reaches the screen sooner. In the owner's tests, heavy games such as Inferno and NyxPPU only gain noticeable FPS at 80 MHz. The 65, 70 and 75 MHz presets were deliberately removed because their results were identical to 60 MHz (the chip can only jump to certain clock values).

**Why it depends on the cable.** The code is fine, but 80 MHz demands short, tidy SCLK and MOSI wires. If the screen becomes **striped, shaky, broken or randomly colored**, the cable has reached its limit; go back down to 60 MHz. The change only takes effect after a restart.

## NyxPPU (a retro-console-style pixel engine) `EXP`

**What it does.** A graphics engine that imitates the *Pixel Processing Unit* concept of classic consoles.

| Component | Specification |
|---|---|
| Tile bank | 40 tiles of 8×8 RGB565 pixels in PSRAM. |
| Background | 2 layers of 48×36 tiles (384×288 virtual pixels), each with its own scroll, giving a parallax effect. |
| Sprites (OAM) | Up to 32 objects with scale, flip, tint and a priority bit (drawn in front of or behind the BG1 layer). |
| Transparency | Magenta `0xF81F` as the *color key*; tile id 0 on BG1 means a hole. |

**Why it is still an experiment.** It is currently only **phase 1**. The engine is real and can be tried in the **PPU Demo** app, but it is not yet used in Inferno or the Home/Lock Screen. Note also that the ESP32-S3 has no GPU, so everything remains software *rendering* on the CPU; the work is optimisation to make it feel as though there is acceleration.
