# Phone calls (SIP and LAN) `PARTIAL`

A voice-calling app over WiFi with two operating modes. The source is `firmware/phone/call_renphone.ino`.

- [Modes](#modes)
- [Technical specification](#technical-specification)
- [Test status](#test-status)
- [Audio path](#audio-path)
- [How to use](#how-to-use)
- [TURN configuration (optional)](#turn-configuration-optional)
- [Things to watch out for](#things-to-watch-out-for)

## Modes

| Mode | Use | Needs internet |
|---|---|---|
| **SIP** | Call another SIP address (for example `friend@sip2sip.info`) through a free account. | Yes |
| **LAN** | Call directly between NyxOS devices on the same WiFi network. | No |

## Technical specification

| Aspect | Detail |
|---|---|
| SIP agent | One agent for both modes, port 5060. |
| Codec | G.711 (PCMU/PCMA), 8 kHz, 20 ms packets, sent over RTP. |
| SIP transport (SIP mode) | **TCP by default.** Can be switched to UDP with `transport=udp` in `sip.cfg`. If the TCP connection fails, the system automatically falls back to UDP. |
| LAN mode transport | UDP. |
| Public address mapping | STUN (`stun.l.google.com:19302`), only for calls over the internet. |
| Relay (optional) | TURN over UDP for CGNAT or strict NAT networks (see below). |

## Test status

Tested on a real device with a `sip2sip.info` account (SIP over TCP): account registration succeeds, and outgoing and incoming calls between the Ren Phone and an Android phone (Linphone app) connect and work normally. A `sip.linphone.org` account was tried and only works in one direction (as a receiver), so `sip2sip.info` is recommended.

What is **not** yet confirmed: LAN mode between two NyxOS devices, TURN, and varied NAT conditions.

## Audio path

```
INMP441 mic ──16 kHz──▶ down to 8 kHz ──▶ G.711 ──RTP──▶ remote party

remote party ──RTP──▶ G.711 ──▶ 8 kHz PCM ──▶ external audio path
                                              ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
```

By default the device has **no speaker**, so the remote party is heard through **Bluetooth TWS** earbuds. Without connected earbuds the microphone still transmits, but you will not hear anything.

**Optional speaker.** With a MAX98357A module wired (see [Hardware](../hardware.md)), set the Music app output to **SPK** (Music > Find TWS > *Output: BT / SPK*). Calls then play straight to the speaker over I2S (8 kHz mono, no CAM or earbuds needed), and the call screen shows *Audio: Speaker*. Starting a call stops any music playing on the speaker. Keep the speaker away from the INMP441 microphone: there is no echo cancellation, so the other person may hear themselves. This path has not been tested on hardware yet.

## How to use

1. Connect the TWS earbuds first, through the **Music** app (or switch the output to **SPK** if you have the speaker module).
2. **SIP mode**
   - Create a free account. Verified: <https://www.sip2sip.info> (set Server to `sip2sip.info`). An account at <https://subscribe.linphone.org> (Server `sip.linphone.org`) was tried and only works as a receiver, so it is not recommended.
   - Open the Phone app, choose *Set*, and fill in Server, username, password and name. Alternatively, place `/sip.cfg` on the SD card (see [Configuration](../configuration.md)).
   - On the SIP tab, type the destination address (for example `friend` or `friend@sip2sip.info`) and press **Call**.
3. **LAN mode**
   - On the LAN tab, enter the IP address of the other device, or press **Find NyxOS phone** (sends a SIP OPTIONS *broadcast*) and pick a result.
   - The receiving device must be on the same WiFi, and the **Ready to receive calls** option must be on.

## TURN configuration (optional)

Without TURN, calls over the internet on CGNAT networks can be silent or one-way. With TURN, the audio is wrapped and passed through a relay server. TURN only applies to SIP calls over the internet; LAN calls are unchanged. If TURN fails, the system falls back to STUN and shows the reason in a *toast*.

- **Through the UI:** Phone → Set → the **TURN >** button (top right). The server can be `host` or `host:port` (default port 3478).
- **Through the SD card:** create `/turn.cfg` (format in [Configuration](../configuration.md)). The file is read automatically only when TURN has not been filled in, or through the *Load SD* button on the TURN page.
- **Cloudflare Realtime TURN:** create a *TURN key* in the dashboard, then generate short-lived credentials (example TTL: 24 hours):

  ```bash
  curl -s -X POST -H "Authorization: Bearer $TURN_KEY_API_TOKEN" -H "Content-Type: application/json" \
    -d '{"ttl": 86400}' https://rtc.live.cloudflare.com/v1/turn/keys/$TURN_KEY_ID/credentials/generate
  ```

  Copy `username` and `credential` from the result into `user=` and `pass=`. The credentials expire per the TTL; regenerate them and overwrite `/turn.cfg` when needed.
- Other TURN servers (for example your own coturn with UDP and *long-term* authentication) can also be used.
- Limitations: TURN over UDP only (no TCP/TLS yet) and no ICE. There are no test notes for TURN yet.

## Things to watch out for

- Only SIP addresses can be called. **Regular phone numbers (PSTN) cannot be reached** through free services.
- Without ICE, calls over the internet can be **one-way or silent** on certain NATs. LAN mode is the most reliable.
- **Not encrypted:** SIP runs over plain TCP/UDP (no TLS yet) and RTP audio is not encrypted. The password is sent as a *hash digest*.
- One-way latency is estimated at **300–500 ms** (because of A2DP).
- The standby mode is **off by default** to save RAM and battery. Incoming calls are rejected automatically while NES or DOOM is running.
- The microphone is shared with other features. Close Voice Memo and Mic Level before calling; Wake Word is paused automatically.
- The original patch also modified `rplink.h` and the CAM firmware. Reflashing the CAM firmware is optional, but makes audio about 140 ms faster.
