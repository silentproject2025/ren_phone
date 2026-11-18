# Security and privacy

This project is intended as a personal, experimental device. Consider the following before using it with sensitive data or networks.

| Area | Current state | Recommendation |
|---|---|---|
| TLS certificates | Not verified (`setInsecure()`) in network features, including Chat. | Do not send sensitive information. |
| SIP calls | SIP signalling runs over plain TCP/UDP (no TLS yet); RTP audio is not encrypted; the password is sent as a *hash digest*. | Use an account dedicated to this device; assume calls can be intercepted. |
| Gutenberg downloads | EPUB/TXT files are downloaded over HTTPS without certificate verification (`setInsecure()`) from `gutenberg.org` and `gutendex.com`. Integrity is checked (a CRC32 of the data on the SD card is compared with the data that arrived from the network, plus the ZIP structure), but the authenticity of the server is not. | Only download from the built-in sources; never open `.bad` files. |
| Web File Manager | No credential check was found on the web server routes (`/`, `/edit`, `/upload`, `/delete`, `/download`, `/apikey`). Other devices on the same network could potentially reach the SD files. | Enable it only on trusted networks. |
| API keys | `gemini_key.txt` and `nasa_key.txt` are stored as plain text on the SD card. | Use keys with limited quotas and keep the SD card physically safe. |
| Remote Control | The AP uses WPA2 with a password generated on first use; access via home WiFi is refused by default; one viewer at a time. | Change the default password and leave *Allow via home WiFi* off unless needed. |
| TURN credentials | Stored on the device or in `/turn.cfg`. | Use short-lived credentials (for example Cloudflare with a TTL). |
| Settings backup | `.nbk` files on the SD card are unencrypted unless encryption is on; WiFi passwords, the SIP account, the Chat account, API keys and the Remote AP password are only included when the *Include passwords and API keys* switch is on. | Leave that switch off, or enable password encryption if secrets must be included; keep the SD card physically safe. |
| Online chess | The broker uses TLS without certificate verification, like Chat; the broker account is stored in NVS and `/chess.cfg` as plain text. | Use a dedicated broker account and do not reuse other passwords. |

## Reporting a vulnerability

If GitHub private vulnerability reporting is enabled for the repository (Security tab), use it; otherwise contact the maintainer directly instead of opening a public issue. Include the affected feature, steps to reproduce and the firmware version or branch.
