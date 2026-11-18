# Legacy patch scripts (archive)

Earlier features were added to the sketch by small, **idempotent** Python/shell scripts (`apply_*.py`, `fix_*.py`, `patch_*.py`) plus two `.patch` files. They have all been applied already: the results are in `firmware/phone/` and the workflows. They are kept here for history and as examples of how each feature was wired in.

> **They were written for the old flat layout** (all sketch files in the repository root) and open files such as `phone.ino` relative to the current directory. To run one against the current sources, execute it from inside the sketch folder:
>
> ```bash
> cd firmware/phone
> python3 ../../tools/legacy-patches/<group>/apply_<name>.py
> ```
>
> Scripts that also edit `.github/workflows/*` or the README refer to paths and text that no longer exist and will not apply cleanly.

| Group | Contents |
|---|---|
| `calling/` | Phone (SIP/LAN/TURN) patches |
| `camera/` | Camera app, GC2145 support, colour fixes |
| `doom/` | DOOM integration and fixes |
| `touch-input/` | Touch, keyboard, scrolling and gamepad patches |
| `ui/` | Home, Control Center, animations, gallery, emoji and other UI patches |
| `build-and-ci/` | CI, libraries, OTA, logging and build-related patches |
| `features/` | Everything else (apps, chat, chess, ebook, backup, i18n, and so on) |

The grouping was done automatically from file names, so a few scripts may sit in a less obvious group. Use `grep -r` to find a specific one.
