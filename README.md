# TelegramNX

Telegram-клиент для Nintendo Switch (homebrew, CFW). Собственный MTProto-стек:
рукопожатие, MTProto 2.0, QR-вход, чаты, история, отправка сообщений, интерфейс
под геймпад и тачскрин.

![Диалоги](docs/screenshots/dialogs.png)
![Вход](docs/screenshots/login.png)

## Что уже работает

- MTProto 2.0 с реальными DC Telegram: DH-рукопожатие, AES-IGE/CTR, RSA_PAD,
  обфусцированный транспорт (obfuscated2), сжатые ответы, обработка контейнеров.
- QR-вход (`auth.exportLoginToken` / `importLoginToken`), в том числе миграция
  на домашний DC; сессия сохраняется между запусками.
- Двухфакторный облачный пароль: SRP-6a (SHA-256 + PBKDF2-HMAC-SHA512),
  `account.getPassword` / `auth.checkPassword`.
- Загрузка списка чатов, истории сообщений и отправка текста
  (`messages.getDialogs`, `messages.getHistory`, `messages.sendMessage`).
- UI: два-панельный список чатов, пузыри сообщений, экран входа с QR, настройки.
- Сборка под macOS (скриншоты/тесты) и **готовый `.nro`** (devkitA64 r29 +
  libnx 4.12, GCC 15.2): `dist/telegramnx.nro` (1.1 МБ) + шрифты в `dist/fonts`.

## Ограничения

- Вход по номеру/SMS-коду выведен в UI (кнопка «Войти по номеру телефона»),
  включая `auth.signUp` для нового аккаунта и 2FA после кода; live-проверка
  требует своего `api_id`.
- Нет медиа (фото/видео/стикеры), редактирования, реакций и push-обновлений:
  список и история обновляются по запросу.
- Нужны собственные `api_id`/`api_hash` с https://my.telegram.org — встраивать
  чужие нельзя.
- `.nro` собран и слинкован, но на реальной консоли не запускался (устройства
  нет). Перед раздачей прогоните его на CFW-Switch.
- Живой вход (QR/номер), список чатов и отправка не проверены без вашего
  `api_id`/`api_hash`; запросы/парсеры покрыты тестами, но нужен end-to-end прогон.

## Готовый `.nro`

В `dist/` лежит собранный комплект:

```
dist/telegramnx.nro
dist/fonts/NotoSans-Regular.ttf
dist/fonts/NotoSans-SemiBold.ttf
```

SHA-256: `ae4d6d9ab767466d4a6119ab66ff2035d1a72fd503082c8a832bd99b5617352c`
(см. `dist/README.txt`). Архив для SD: `telegramNX-nro.zip`.

## Сборка для Switch

1. Установите devkitPro: https://devkitpro.org/wiki/Getting_Started
   (macOS: установщик `devkitpro-pacman-installer.pkg`, затем
   `sudo dkp-pacman -S switch-dev switch-zlib`).
2. Экспортируйте окружение:
   ```sh
   export DEVKITPRO=/opt/devkitpro
   export DEVKITA64=$DEVKITPRO/devkitA64
   export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH
   ```
3. Соберите:
   ```sh
   make
   ```
   Результат: `build_switch/telegramnx.nro` (проверено на devkitA64 r29,
   GCC 15.2.0, libnx 4.12.0; сборка чистая, без предупреждений).
   Готовый workflow для GitHub Actions лежит в
   `.github/workflows/build-switch.yml` (собирает `.nro` в контейнере
   devkitPro).
4. Скопируйте на SD-карту:
   ```
   /switch/telegramnx.nro
   /switch/telegramnx/fonts/NotoSans-Regular.ttf
   /switch/telegramnx/fonts/NotoSans-SemiBold.ttf
   ```
   Шрифты лежат в `assets/fonts/`.
5. Запустите через hbmenu. При первом запуске введите `api_id` и `api_hash`
   (виртуальная клавиатура), затем отсканируйте QR в Telegram:
   Настройки → Устройства → Подключить устройство.

Конфиг и сессия: `sdmc:/switch/telegramnx/config.ini` (содержит auth key —
не делитесь файлом).

## Сборка и проверка на macOS

```sh
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build --output-on-failure   # крипто/парсер-тесты + switch_syntax
./build/live_auth                             # живое DH + ping к DC2
./build/live_config                           # RPC + gzip (help.getConfig)
./build/ui_shots                              # скриншоты экранов
```

`live_auth` и `live_config` ходят в реальный Telegram; `live_qr` требует
`--api=... --hash=...`.

Тестовый DC и телефонный вход (нужен свой `api_id`/`api_hash`):

```sh
./build/live_phone --api=ID --hash=HASH \
  --ip=149.154.167.40 --port=80 --dc=10002 \
  --phone=+9996621234 --code=22222
```

Номер `99966XYYYY`, код — `X` пять раз (тестовые аккаунты Telegram,
см. core.telegram.org/api/auth). Скрипт сам сделает `auth.signUp`, отправит
сообщение в «Избранное» и перечитает историю/диалоги.

## Структура

```
src/core/    TL, криптография, DH, MTProto, RPC, API, клиентский поток
src/ui/      канвас, шрифты, экраны
src/platform/  macOS-хост и Switch (libnx)
third_party/ stb_truetype, qrcodegen
tools/       генератор TL-таблиц и референсы протокола
```

## Лицензия

Код — MIT. Шрифты Noto Sans — OFL 1.1. `stb_truetype` — public domain/MIT,
`qrcodegen` — MIT.
