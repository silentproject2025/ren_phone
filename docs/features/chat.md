# Chat over an MQTT broker `OK`

Private messaging between devices **without a self-built server**. An MQTT broker (TLS, port 8883, username and password authentication) acts as the server. The firmware is not tied to one provider; switching brokers only means changing the configuration values on the phone.

- [How it works](#how-it-works)
- [Configuration](#configuration)
- [Automatic broker login](#automatic-broker-login-beta)
- [Choosing a broker](#choosing-a-broker)
- [What is and is not verified](#what-is-and-is-not-verified)
- [Limitations](#limitations)
- [Alternatives](#alternatives-if-emqx-changes-too)

## How it works

- **History is stored on the broker.** Every message is published as a *retained* message to one of 24 rotating slots (`renphone/chat/m/<slot>`). When a device connects, the broker immediately sends the content of those 24 slots, so the last 24 messages appear, including those sent while the Chat app was closed.
- **Message format:** `seq|time|name|message`. The `seq` number is used for ordering and for discarding duplicates.
- **RAM-friendly.** The device is only connected while the Chat app is open. After it is closed, the TLS connection is released and the memory returned.
- A topic `renphone/chat/t` (non-retained, QoS 0) carries the "is typing" indicator with the sender's name; it is already covered by an ACL on `renphone/chat/#`.

## Configuration

If no login is fetched automatically (see [below](#automatic-broker-login-beta)), fill it in through the app (Broker, Username, Password, Name, then *Save & connect*; this screen only appears while no login is stored), or put a `/chat.cfg` file on the SD card and press *Load SD* (see [Configuration](../configuration.md)). Typing on the on-screen keyboard is tiring, so the SD file is often more convenient:

```
host=xxxxxxxx.ala.eu-central-1.emqxsl.com
user=yourusername
pass=yourpassword
name=Ren
```

Then open the Chat app (or press *Load SD* on the settings screen).

## Automatic broker login `BETA`

Nothing has to be typed if the project owner has set up the Supabase side. When WiFi is connected and Chat is opened with **no broker login stored**, the phone calls the Supabase function `get_broker_config('chat', <device id>)` once and stores the host, username and password on the phone (NVS). Not yet tested on a device.

- **Fetched once.** As soon as a login is stored, Chat no longer contacts Supabase.
- **Your own login is safe.** A login you typed, or loaded from `/chat.cfg`, is never overwritten by the automatic fetch.
- **Refetch button** (top right in Chat, it replaced the old *Set* button). It asks Supabase for the current login and replaces the stored one, also a typed one. Use it after the password was changed on the broker. If the request fails, the old login stays and a toast shows `Gagal ambil broker (code)`.
- **Codes in that toast:** an HTTP status as is (for example `401` or `404`), `-1` network or TLS failure, `-100` not enough internal RAM, `-101` the request could not start, `-102` Supabase answered but the login is empty or switched off.
- **Fallback.** If nothing is stored and the fetch fails, the manual screen (Broker, Username, Password, Name) appears as before, and `/chat.cfg` is still read.
- **Off switch.** Leave `RP_REG_URL` in `firmware/phone/devreg_conf.h` empty and the whole feature does nothing.

**Setup (once, by the project owner).** In the Supabase SQL Editor run `supabase/setup.sql`, then `supabase/broker_config.sql`. Replace the `GANTI_USER_*` / `GANTI_PASSWORD_*` placeholders and the hosts with your own broker accounts. The table has one row per service (`chat` and `chess`, two separate brokers). Setting `enabled = false` on a row switches it off for every phone without a firmware update.

**Security.** The publishable key is in the public firmware, so anyone can call the function and read the login. Treat it as not secret: create broker users only for this project and restrict them by ACL (Chat: `renphone/chat/#`; Chess: its own topics), never an admin account. See [Security](../security.md).

## Choosing a broker

**HiveMQ Cloud Serverless (free plan) — do not use.** It is being discontinued: since 2026-09-30 new clusters cannot be created, it stops working on 2026-12-31, and its data is deleted on 2027-01-07. Older guides that describe HiveMQ as free no longer apply.

**EMQX Cloud Serverless** is a free option that was checked. According to the EMQX plan page at the time it was checked, the free monthly quota is 1 million session minutes, 1 GB of traffic, 1 million rule actions and up to 1,000 connections, with TLS. Serverless has limits on connections, throughput, subscriptions and connection methods. The "no credit card" claim was only found on third-party sites, so verify it yourself when signing up. After the HiveMQ episode, re-check the terms before depending on any provider.

1. Sign up at <https://www.emqx.com/en/cloud>, create a project, then create a **Serverless** deployment.
2. In **Overview**, note the **connection address (host)**. Port **8883 (mqtts)** is already enabled by default; Serverless requires TLS. The address looks like `xxxxxxxx.ala.eu-central-1.emqxsl.com` (use exactly what your console shows).
3. In **Access Control → Authentication** (called Client Authentication in other documentation): click **Add**, enter a **username + password** dedicated to the phone, then Confirm. Access Control also has authorization (ACL) settings; optionally restrict that credential to the topic `renphone/chat/#` (publish + subscribe). Menu names may differ slightly because the console is updated often.

Chess uses its own, separate broker settings; see [Chess](./chess.md#broker). If the broker account is restricted by ACL to `renphone/chat/#`, add publish and subscribe permission for `renphone/chess/#`.

## What is and is not verified

- From EMQX documentation (Serverless limits): retained messages are supported, up to 2,000 messages, with a maximum size of 1 MB per message, and they do not expire. Chat only uses 24 retained messages, far below the limits. That limits document looks somewhat old, so test it yourself.
- Serverless requires SNI = the connection address. The Chat app connects using the host name, so SNI is sent; however, this has not been tried against a real broker.
- The number of Serverless deployments is limited to 2; a second one requires a payment method. One deployment is enough.
- An earlier version of this guide stated that the code had not yet been tried against a real broker; the project status now lists Chat as tested with a real broker (`OK`). The broker-specific details above (SNI, retained-message limits) were not individually confirmed. To test history: send a message from phone A, close the app on phone B, send again from A, then open the app on B; the history should appear.

## Limitations

- No notification while the app is closed; new messages are seen as soon as Chat is opened.
- Two phones sending in exactly the same second can collide on the same slot (one message disappears from the history).
- The screen displays ASCII and emoji only (other letters appear as `?`). Usernames are ASCII only.
- The connection uses `setInsecure()` (the broker certificate is not verified), like other network features in this firmware. Do not send anything sensitive. See [Security](../security.md).

## Alternatives if EMQX changes too

- Your own broker (for example Mosquitto) on a PC/Raspberry Pi: free, but reaching it from outside the home needs port forwarding or a tunnel.
- Your own server on Cloudflare Workers + Durable Objects (free plan, as checked earlier). That requires a version of the Chat app that uses HTTP polling instead of MQTT.
