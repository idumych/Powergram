# Powergram

A Telegram client for [Haiku](https://www.haiku-os.org) and
[Renku OS](https://github.com/RenkuOS/Source), written with the native Haiku
API after the GNOME app [Paper Plane](https://github.com/paper-plane-developers/paper-plane).
It talks to Telegram through [TDLib](https://github.com/tdlib/td), Telegram's
own client library, and keeps Paper Plane's layout: the chat list on the left,
the open chat on the right.

**Preview.** Version 0.1.0, a first preview for Haiku and Renku OS (x86_64),
came out under the app's earlier name, Paper Plane for Haiku: download the
`.hpkg` from [Releases](https://github.com/idumych/paper-plane-haiku/releases)
and install it with `pkgman install ./paperplane-0.1.0-1-x86_64.hpkg`. The
next release is named Powergram and replaces it, keeping your login and
settings.

![Powergram with a group chat open](docs/screenshots/haiku-group.png)

## What works

Built and run on Renku OS x86_64. Used with a real account: logging in, the
chat list, reading chats, photos in chats, sending and editing messages,
emoji, stickers and notifications. Videos, GIFs, round video messages,
sending photos and files, spoilers and self-destructing photos have only been
checked with made-up data so far.

- Logging in: phone number, code, cloud password, new accounts
- Chat list with profile pictures, unread and mention badges, pinned chats
- Reading chats: replies, forwards, edits, read ticks, date separators,
  sender pictures in groups, older messages loading as you scroll up
- Photos, videos, GIFs, round video messages and stickers (WebP; animated
  stickers show their emoji); spoilers and self-destructing photos
- Sending text, photos, files (file panel or drag and drop from Tracker) and
  stickers; replying, editing, deleting, copying
- Emoji and sticker pickers
- Notifications that follow each chat's mute settings
- Read receipts, typing status and online status sent the way the official
  apps send them, and every request throttled (a few at a time, and a full
  stop when Telegram asks to wait)

Not yet: voice messages, calls, secret chats, search, QR-code login, media
playback inside the app, a Linux (Cosmoe) build, an app icon.

## Screenshots

The real app windows filled with made-up chats (`tests/mockup.cpp`); no real
account appears in them.

![A private chat with a photo, a video and a round video message](docs/screenshots/haiku-media.png)
Photos load blurred first; videos show their length; round video messages are drawn round.

![A spoiler, a sticker and a GIF](docs/screenshots/haiku-media2.png)
Spoilers stay blurred until clicked; WebP stickers keep their transparency.

![The emoji picker](docs/screenshots/haiku-emoji.png)
The emoji picker, recently used first.

![The sticker picker](docs/screenshots/haiku-stickers.png)
The sticker picker: your sets on the left, their stickers on the right.

![Replying to a message](docs/screenshots/haiku-reply.png)
Replying: the message being answered is shown over the entry.

## Platforms

| Platform | State |
|---|---|
| Renku OS / Haiku R1/beta6, x86_64 | Builds and runs |
| Haiku arm64 | Not yet: TDLib comes from HaikuPorts, which has no arm64 repository |
| Linux (Cosmoe) | Not yet |

## Building

On Haiku or Renku OS:

```sh
pkgman install tdlib openssl3_devel zlib_devel
make
./Powergram
```

`sh packaging/build-hpkg.sh` builds the package.

HaikuPorts' TDLib is static only, so it's linked into the app; the result
needs only OpenSSL and zlib at run time. Install `noto_emoji` for emoji.

`make test` runs the core tests (login flow, chat list, history, throttling,
files, stickers) on any system with a C++17 compiler.

## Your API key

Telegram wants every client to use an API ID of its own, and this app ships
none. At first start Powergram opens [my.telegram.org](https://my.telegram.org)
in your browser: log in, choose "API development tools", create an app, and
copy its api_id and api_hash into Powergram's window (pasting both lines at
once fills both fields). For scripted setups the key can instead go in a text
file `~/config/settings/Powergram/api_key`:

```
api_id=1234567
api_hash=0123456789abcdef0123456789abcdef
```

Powergram reads that file once, keeps the key in its own settings (readable
only by you) and deletes the file.

`--test-dc` connects to Telegram's test servers instead. Telegram no longer
accepts its `99966…` test numbers there; a test account has to be created
with an official mobile app first.

## Notes for Haiku developers

- HaikuPorts builds TDLib without threads (`TD_THREAD_UNSUPPORTED`), so
  `td_send` and `td_receive` must come from the same thread; `TdClient`
  queues requests for its one TDLib thread.
- `MessageView` sets `B_SCROLL_VIEW_AWARE`: without it `BScrollView` resets
  the scroll range from the view's preferred size whenever its height changes.
- Text is measured once per message with `BFont::GetEscapements`; measuring
  each candidate line with `StringWidth` stalled `app_server`.

## Licence

GPL-3.0-or-later (see `LICENSE`), as Paper Plane. Includes
[nlohmann/json](https://github.com/nlohmann/json) (MIT) in `third_party/`.
