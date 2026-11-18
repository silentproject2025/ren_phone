# Contributing

Thank you for your interest. This is a personal, hardware-dependent project, so **test reports from real devices are the most valuable contribution**.

## What needs testing most

In order of value:

1. **Phone calls, LAN mode** between two NyxOS devices (the fewest variables).
2. **Phone calls, SIP mode** to a provider other than `sip2sip.info` (`sip.linphone.org` was tried and only works in one direction).
3. **Phone calls with TURN** on a CGNAT or strict NAT network.
4. **Camera** on another unit, especially with a sensor other than the OV2640 and a longer UART cable.
5. **Online chess** between two NyxOS devices (rules, move synchronisation, disconnect/reconnect, the AI taking over on AFK) and **vs. AI** at every level.
6. **Backup & Restore**: create and restore, with and without encryption, then check that the settings return after a restart.

For phone-call tests, note whether it rings, whether audio is two-way, how clear it is, and whether the device restarts.

## Reporting a bug

Please include:

- the feature and the steps that trigger the problem;
- the firmware version or branch (the *About Phone* or *Update* app);
- a HWmonitor screenshot;
- the Serial Monitor log, if available;
- whether the Remote AP and Bluetooth were on.

A bug report template is provided under **Issues → New issue**.

## Working on the code

- The repository layout is described in the [README](./README.md#repository-layout) and in [firmware/README.md](./firmware/README.md).
- Build instructions, including how to register a new `.ino` file with CI, are in [docs/building.md](./docs/building.md#working-on-the-code).
- Run the host-side tests that cover the code you touched; see [tests/README.md](./tests/README.md).
- Keep `rplink.h` **identical** in `firmware/phone/` and `firmware/cam_bt_coprocessor/`.
- Change history is written as comments directly in the code, covering the symptom, the root cause and the reason for the fix. To understand why something was written a certain way, start with the comment above it.
- UI text should go through `RPTR("...")` and have an entry in `tools/i18n_pairs.txt`; see [docs/localization.md](./docs/localization.md).

## Pull requests

- Keep a pull request focused on one change.
- Describe what you tested and on which hardware (display variant, ESP32-CAM sensor, and so on).
- Mark anything you could not test on a device, as the documentation does with `BETA`.
