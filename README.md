# Powergram

A Telegram client for [Haiku](https://www.haiku-os.org) and
[Renku OS](https://github.com/RenkuOS/Source), written with the native Haiku
API after the GNOME app [Paper Plane](https://github.com/paper-plane-developers/paper-plane).
It talks to Telegram through [TDLib](https://github.com/tdlib/td), Telegram's
own client library, and keeps Paper Plane's layout: the chat list on the left,
the open chat on the right.

**Preview.** Version 0.2.10 is a preview for Haiku, Renku OS and Linux.
From [Releases](https://github.com/idumych/Powergram/releases):

- Haiku and Renku OS (x86_64): `pkgman install ./powergram-0.2.10-1-x86_64.hpkg`
  (an arm64 package is there too, for the Haiku arm64 nightlies)
- Linux (x86_64 or aarch64, glibc 2.39 or newer, X11 or Wayland):
  `chmod +x Powergram-0.2.10-x86_64.AppImage && ./Powergram-0.2.10-x86_64.AppImage`
  (`-aarch64` on ARM)

The app was called Paper Plane for Haiku up to 0.1.0; Powergram replaces
that package and keeps your login and settings.

![Powergram with a group chat open](docs/screenshots/haiku-group.png)

## What works

Built and run on Renku OS and Haiku R1/beta6, x86_64. Used with a real
account (0.1.0): logging in, the chat list, reading chats, photos in chats,
sending and editing messages, emoji, stickers and notifications. Everything
added since 0.2.0, and videos, GIFs, round video messages, sending photos and
files, spoilers and self-destructing photos, has only been checked with
made-up data so far.

- Logging in: phone number, code, cloud password, new accounts, or a QR code
  scanned with Telegram on your phone (Settings > Devices > Link Desktop Device)
- Your own API key, entered in the login window: it opens my.telegram.org/apps
  in your browser, and pasting the api_id and api_hash lines fills both fields
- Chat list with profile pictures, unread and mention badges, pinned chats,
  drafts, archived chats and chat folders; right-click to mark read or
  unread, mute, pin, archive or delete
- Search: filter the chat list by name, find messages in a chat (Cmd+F, Cmd+G)
- Telegram links and @usernames open in the app: public chats, invite links
  (asking first), links to messages; Chat > Open username or link (Cmd+O);
  a Join button in groups and channels you're not in
- Reading chats: replies, forwards, edits, read ticks, date separators,
  sender pictures in groups, older messages loading as you scroll up; bold,
  italic, code, spoilers and clickable links; opening at the first unread
  message, with a jump-to-latest button; messages of a single emoji shown large
- Selecting text in messages (drag, double- or triple-click) and copying it
- Haiku vector icons (HVIF files) sent in chats, drawn in the message
- Typing, online and "last seen" status and group sizes in the chat header
- Pinned messages: a bar under the chat's name (click to jump, and again for
  the next one); pin and unpin from the message's menu
- Reactions (shown and added), forwarding, polls, locations, places and contacts
- Voice messages, played in the app with a waveform, and recorded: click the
  microphone, then Send (or the microphone again); Escape cancels
- Files and music sent to you, shown with their type's icon, name and size:
  click to open one in its application, or "Save as…" from its menu
- A chat info window: picture, bio or description, members, shared photos
- Your profile (Powergram > Profile): picture, name and bio, and the devices
  logged in to your account, any of which you can log out
- Settings: notifications, message text in notifications, a sound (chosen in
  the Sounds preferences), text size, and where photos and videos open
- Touch screens: drag the chat list, chats, pickers and other lists with a
  finger, as on a phone: they keep moving after a flick and stretch at the
  ends (Settings > Scroll by dragging also offers it for the mouse); a tap
  still clicks
- Photos and videos open in their own apps (ShowImage, MediaPlayer) as
  before, or, if Settings say so, in Powergram's viewer: videos play with
  sound, Space or a click pauses, the slider or the arrow keys seek, GIFs
  loop; a button opens the file in its app anyway. The message menu always
  offers both
- Secret chats (Chat > New secret chat): end-to-end encrypted, on this
  computer only; a green padlock in the chat list, a self-destruct timer
  (Chat > Self-destruct timer), notifications without the text, and the
  encryption key in Info as a picture to compare with the other phone
- Photos, videos, GIFs, round video messages and stickers; animated
  stickers play, both Lottie (.tgs, drawn by Powergram itself) and video
  (.webm, with libvpx) ones, in chats and as the first frame in the picker;
  spoilers and self-destructing photos
- Sending text, photos, videos (MP4 and QuickTime, with their size and
  length), GIFs (as GIFs, which keep moving), files (file panel or drag and
  drop from Tracker) and stickers; several photos and videos at once go as
  an album; replying, editing, deleting, copying
- Before files go, a Send window shows them (pictures as thumbnails) with a
  caption field; pasting a picture (Alt+V, e.g. copied in ShowImage or a web
  page) or files copied in Tracker into the message box sends them the same way
- Right-click Send to send without sound, schedule the message for a day
  and time, or send it when the other person comes online; Chat > Scheduled
  messages lists them, to send now, reschedule or delete
- Polls and quizzes (Chat > New poll), and locations from coordinates or a
  link copied from a map website (Chat > Send location)
- Contacts and new chats (Chat > New chat, Cmd+N): your contacts with their
  status, searched by name, username or phone number, public chats and
  channels found by name; new groups and channels with the contacts you
  tick; adding a contact by phone number
- Mentions: typing @ in a group suggests its members (Up, Down, Enter or Tab,
  or a click); people without a username are mentioned by name. An "@" button
  with the count goes to your oldest unread mention
- Emoji and sticker pickers
- Notifications that follow each chat's mute settings
- A Deskbar icon with the number of unread chats: click it to bring Powergram
  forward, drop files on it to send them to a chat you pick (Settings can
  turn it off)
- Read receipts, typing status and online status sent the way the official
  apps send them, and every request throttled (a few at a time, and a full
  stop when Telegram asks to wait)

Not yet: calls.

On Linux, Powergram is the same app built with
[Hokku](https://github.com/idumych/hokku), the Haiku API as a library on X11
and Wayland. What differs there: voice messages and videos open in your
desktop's player (Hokku decodes no Opus or video yet), and videos you send
go as files. Recording voice messages uses the default sound input. Emoji
need the Noto Color Emoji font, which most desktops have. The unread count
needs Hokku's Deskbar or a tray; it hasn't been tried in a desktop's tray
yet.

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

![Voice messages](docs/screenshots/haiku-voice.png)
Voice messages play in the app; the waveform fills as they play.

![Files in a chat](docs/screenshots/haiku-files.png)
Files show their type's icon from Haiku's file types, and their progress while downloading.

![Opening a username, with a Join button](docs/screenshots/haiku-links.png)
Mentions and t.me links open in the app; Cmd+O opens a typed username or link. In a group you're not in, Send becomes Join.

![A pinned message](docs/screenshots/haiku-pinned.png)
The pinned bar: click to jump to the message, and again for the next one.

![The chat info window](docs/screenshots/haiku-info.png)
Chat info: the description, members and shared photos.

![The Profile window](docs/screenshots/haiku-profile.png)
Your profile, and the devices logged in to your account.

![The New chat window](docs/screenshots/haiku-newchat.png)
New chat (Cmd+N): your contacts, found as you type, and new groups and channels.

![A new poll](docs/screenshots/haiku-poll.png)
A poll or quiz: a new answer field appears as the last one is filled.

![Scheduled messages](docs/screenshots/haiku-scheduled.png)
A chat's scheduled messages, to send now, reschedule or delete.

![The Settings window](docs/screenshots/haiku-settings.png)
Settings apply at once; here the text size is Large. Photos and videos open in their own apps unless you choose Powergram.

![Powergram's video viewer](docs/screenshots/haiku-viewer.png)
Powergram's viewer playing a video, with a button to open it in MediaPlayer instead.

![A secret chat and its encryption key](docs/screenshots/haiku-secret.png)
A secret chat: the padlock in the chat list, and the encryption key to compare with the other phone.

![Logging in with a QR code](docs/screenshots/haiku-qr.png)
Logging in with a QR code (this one is made up).

![Powergram on Linux with the emoji picker open](docs/screenshots/linux-emoji.png)
The same window on Linux (X11), with the emoji picker in Noto Color Emoji.

## Platforms

| Platform | State |
|---|---|
| Renku OS / Haiku R1/beta6, x86_64 | Builds and runs |
| Haiku arm64 (nightlies) | Preview: runs on the hrev60397 nightly in QEMU. Haiku arm64 has no Media Kit or image translators yet, so voice messages, the video viewer, video stickers and pictures don't work there |
| Linux, x86_64 and aarch64 (AppImage, built with Hokku) | Preview: the login, the main window and the pickers run under X11 here, and CI starts both AppImages under X11 and Wayland. Not yet used with a real account. Voice messages and videos open in the desktop's player |

## Building

On Haiku or Renku OS:

```sh
pkgman install tdlib openssl3_devel zlib_devel opus_devel libogg_devel libvpx_devel
make
./Powergram
```

`sh packaging/build-hpkg.sh` builds the package.

For Haiku arm64, cross-compiled on Linux (TDLib, OpenSSL and zlib are built
for arm64 and linked in):

```sh
sh tools/haiku-arm64/make-sysroot.sh ~/.cache/haiku-arm64   # once, ~20 min
. ~/.cache/haiku-arm64/env.sh
sh tools/haiku-arm64/build-deps.sh                          # once, ~15 min
sh packaging/build-hpkg-arm64.sh
```

On Linux, with [Hokku](https://github.com/idumych/hokku) installed (or
staged) and TDLib built by `packaging/appimage/build-tdlib.sh`:

```sh
make COSMOE_ROOT=/usr TD_ROOT=<TDLib prefix>
./Powergram
```

`sh packaging/appimage/build-appimage.sh` builds the AppImage (and Hokku and
TDLib, if not given); release AppImages are built by CI on Ubuntu 24.04.

HaikuPorts' TDLib is static only, so it's linked into the app; the result
needs only OpenSSL and zlib at run time. Install `noto_emoji` for emoji.

`make test` runs the core tests (login flow, chat list, history, throttling,
files and download progress, stickers, formatting, presence, voice, chat info,
links and usernames, pinned messages, voice recording, mentions, profile and devices,
QR login, secret chats, contacts and new chats, scheduled messages, polls,
locations, Lottie animations and WebM stickers) on any system with a C++17 compiler, and `make qr-check`
reads the login QR code back with zbarimg.
On Haiku, `make file-saving-test` checks saving received files and
`make voice-record-test` checks the voice message files it records and
`make media-probe-test` what files are sent as (photo, video or document) and
`make video-playback-test` the viewer's video playback (timing, pausing,
seeking, the end, looping, and sound staying with the picture).
`make lottie-render` builds a tool that draws frames of an animated sticker
(`.tgs`, Lottie `.json` or `.webm`) into a PNG.

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
