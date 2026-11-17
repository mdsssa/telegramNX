# TelegramNX

[![build-linux](https://github.com/mdsssa/telegramNX/actions/workflows/build-linux.yml/badge.svg)](https://github.com/mdsssa/telegramNX/actions/workflows/build-linux.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A native **Telegram client for Nintendo Switch homebrew** (CFW), written from
scratch on top of its own MTProto 2.0 stack. It also builds as an **SDL2 app for
macOS**, so login and chats can be tested without touching the console.

[Русский](README.ru.md)

![Chats](docs/screenshots/dialogs.png)
![Login](docs/screenshots/login.png)

> **Status:** experimental. The macOS host app builds and starts, the Switch
> `.nro` builds and links, but on-device startup is still being validated on
> real hardware. Telegram requires your own **`api_id`/`api_hash`** — see below.
>
> Prebuilt experimental bundle: [Releases](https://github.com/mdsssa/telegramNX/releases/tag/v0.1.0).

## Features

- Custom MTProto 2.0 implementation: DH exchange, `RSA_PAD`, AES-256-IGE/CTR,
  obfuscated2 transport, `msg_container`, gzip, salts and message ids.
- QR login (`auth.exportLoginToken` / `importLoginToken`), including DC migration.
- Phone-number login: `auth.sendCode` → `auth.signIn` → `auth.signUp`.
- Cloud password / 2FA via SRP-6a (SHA-256 + PBKDF2-HMAC-SHA512,
  `account.getPassword` / `auth.checkPassword`).
- Dialogs, message history and text sending
  (`messages.getDialogs`, `messages.getHistory`, `messages.sendMessage`).
- Schema-driven TL parser (layer 233, 600+ constructors), so messages with media,
  reactions and premium flags do not break the parser.
- Switch UI: two-pane chat list, message bubbles, QR screen, settings;
  gamepad + touchscreen + `swkbd`.
- macOS SDL2 UI: same `App` and `TelegramClient`, keyboard/mouse input, modal
  prompts for API credentials, phone code and 2FA password.
- Session is persisted; the next launch resumes the saved auth key.

## API credentials (important)

Telegram requires every third-party client to identify itself with an
application `api_id` and `api_hash`. There is **no legitimate way around this**
for a personal-account MTProto client. The values are created at
[my.telegram.org](https://my.telegram.org) → **Development Tools** →
**Create application**.

- `api_id` is a number, `api_hash` is 32 hex characters. This is **not** a
  `@BotFather` bot token.
- Telegram blocks app registration from VPN/hosting/datacenter IPs with
  “Too many attempts”. Use a residential connection, mobile data, or ask
  someone with a residential connection to create the app.
- Do **not** reuse API credentials extracted from official or third-party
  clients. That violates Telegram's rules and can get accounts restricted.

### Distributing a fork

End users do **not** each need their own `api_id`. A fork can register one
application and ship it as the default, so users only scan a QR code and log in
with their own account. The credentials are extractable from the binary, so
abuse can get the application limited; keep the app name without “Telegram” and
comply with Telegram's API terms. The current code asks for credentials on the
first run; embedding defaults is a small follow-up change.

## Quick start (macOS, no Switch needed)

```sh
brew install sdl2
cmake -S . -B build
cmake --build build -j8
./build/telegramnx_host
```

The host app uses `~/.config/telegramnx/config.ini` and writes connection
diagnostics to `~/.config/telegramnx/host.log`.

Controls: arrows, `Z` = A, `X` = B, `C` = X, `V` = Y, `Esc` = back. In a chat,
type on the keyboard, `Enter` sends, `Backspace` deletes. Mouse clicks work too.

## Build for Nintendo Switch

1. Install devkitPro and the Switch package set:
   ```sh
   sudo dkp-pacman -S switch-dev switch-zlib
   export DEVKITPRO=/opt/devkitpro
   ```
2. Build:
   ```sh
   make
   ```
   Output: `build_switch/telegramnx.nro`.

Toolchain pairing matters: this project is verified with devkitA64 r29.1
(GCC 15.2.0), **newlib 4.6** and **libnx 4.12.0**. libnx 4.12 with newlib 4.5 can
crash in `fsdev_chdir` before `main` on firmware 22.x. A libnx 4.11.1 fallback
build is included as `telegramnx-411.nro` for older toolchains.

GitHub Actions workflows are in `.github/workflows/`: one builds the host tests,
one builds the `.nro` in the `devkitpro/devkita64` container.

### SD card layout

```text
/switch/telegramnx.nro
/switch/telegramnx/fonts/NotoSans-Regular.ttf
/switch/telegramnx/fonts/NotoSans-SemiBold.ttf
```

Launch `telegramnx.nro` from hbmenu. On first run enter `api_id`/`api_hash`
with the on-screen keyboard, then scan the QR code in Telegram:
Settings → Devices → Link Desktop Device.

Session and config: `sdmc:/switch/telegramnx/config.ini` (contains an auth key —
do not share it).

### If the NRO does not start

The bundle also contains `hello.nro` (libnx 4.12 + newlib 4.6) and
`hello-411.nro` (libnx 4.11 + newlib 4.5). If both hello builds crash, the issue
is in the hbmenu/Atmosphère/firmware setup, not in this app. If an app crashes
after `main`, read `sdmc:/switch/telegramnx/boot.log`; it records the last
stage (`main entered`, `fonts ok`, `framebuffer ok`, `config`, `render loop`).

## Linux build check

A small Linux CI job builds the portable core and runs the unit tests without
SDL (the SDL2 host UI is macOS-oriented):

```sh
sudo apt-get install -y cmake g++ zlib1g-dev
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug -DTGNX_BUILD_HOST_UI=OFF
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

It covers `crypto_test`, `api_test`, `parse_test` and `switch_syntax`; the
`build-linux` workflow runs the same commands on Ubuntu. The Linux job does not
exercise libnx or the Switch runtime, only portable code and TL parsers.

## Tests and diagnostics

```sh
ctest --test-dir build --output-on-failure   # crypto, TL parser, Switch syntax
./build/live_auth                             # real DH handshake + ping to DC2
./build/live_config                           # RPC + gzip (help.getConfig)
./build/ui_shots                              # UI screenshots
```

`live_qr` and `live_phone` require a real `api_id`/`api_hash`:

```sh
./build/live_qr --api=ID --hash=HASH
```

Telegram test DCs use phone numbers `99966XYYYY` and the code `XXXXX`
(see <https://core.telegram.org/api/auth#test-accounts>).

## Known limitations

- Live QR/phone login, dialogs and sending have not been end-to-end tested with
  a real account in this environment; protocol requests and parsers are covered
  by tests and live handshake checks.
- 2FA SRP is implemented and validated against a reference vector, but not
  exercised against a live 2FA account.
- No media rendering, editing, reactions, or server push updates yet. The chat
  list and history are refreshed by polling.
- On-device Switch startup is still being validated; use the hello builds and
  `boot.log` for diagnosis.

## Layout

```text
src/core/      TL codec, crypto, DH, MTProto, RPC, API, client thread
src/ui/        software canvas, bitmap fonts, screens
src/platform/  macOS SDL2 host, Switch (libnx) platform
third_party/   stb_truetype, qrcodegen
tools/         TL table generator and protocol references
```

## Security and license

The config file stores the auth key; treat it as a secret. Do not commit or share
`config.ini` or `host.log`.

Code is **MIT** (OSI-approved). See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md)
for third-party components and trademark notes. Noto Sans fonts are OFL 1.1
(`assets/fonts/OFL.txt`); `stb_truetype` is public domain/MIT; `qrcodegen` is MIT.

TelegramNX is unofficial and not affiliated with Telegram or Nintendo. You must
provide your own Telegram API application credentials and own the console you
run it on.
