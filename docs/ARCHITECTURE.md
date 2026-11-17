# Архитектура TelegramNX

## Слои

```
platform/  потоки, сокеты, время, файлы, framebuffer, hid, swkbd
core/      TL, криптография, MTProto, RPC, API, фоновый клиент
ui/        программный канвас, растровые шрифты, экраны
```

`core` не зависит от libnx: хост-сборка использует те же файлы, что и Switch.

## Путь одного сообщения

1. `ui::App` → `TelegramClient::sendText` кладёт команду в очередь.
2. Фоновый поток (`platform::Thread`) вызывает `api::sendText`, который
   сериализует `messages.sendMessage` TL-писателем.
3. `mt::RpcClient` оборачивает запрос в `invokeWithLayer(initConnection(...))`,
   шифрует MTProto 2.0 (AES-256-IGE, msg_key = SHA-256) и отправляет через
   `mt::ObfuscatedTransport` (64-байтовый заголовок + AES-CTR).
4. Ответ приходит как `rpc_result`, возможно внутри `msg_container` и/или
   `gzip_packed`; RPC-слой распаковывает и сопоставляет по `req_msg_id`.
5. `ui` получает событие `Sent` и перезапрашивает историю.

## Замечания по протоколу

- TL-строки в MTProto сериализуются **не** 4-байтовой длиной: 1 байт при
  длине ≤ 253, иначе 0xFE + 3 байта длины. Это же относится к `pq` в `resPQ`.
- DH: RSA_PAD (OAEP+ вариант), проверка `dh_prime` по встроенному значению,
  `new_nonce_hash1` = последние 16 байт SHA-1(new_nonce + 1 + aux_hash).
- Для зашифрованных сообщений SHA-1 в DH-ответе покрывает только TL-объект,
  без случайного паддинга.
- QR-логин: `auth.exportLoginToken` → `tg://login?token=...`; опрос каждые
  ~2 секунды; при `loginTokenMigrateTo` создаётся ключ на целевом DC и
  вызывается `auth.importLoginToken`.
- 2FA: при `SESSION_PASSWORD_NEEDED` вызывается `account.getPassword`, затем
  SRP-6a (`x = SHA256(salt2 + PBKDF2-HMAC-SHA512(SHA256(salt2+hash1+salt2),
  salt1, 100000) + salt2)`, `S = (B - k*g^x)^(a+u*x) mod p`) и
  `auth.checkPassword` с `inputCheckPasswordSRP`. Формулы проверены тестовым
  вектором в `tests/crypto_test.cpp`.

## Таблица TL

`tools/gen_tl.py` читает `tools/reference/api.tl` (слой 233), вычисляет
замыкание типов от `User`, `Chat`, `Dialog`, `Message`, `Peer`, `Config` и
генерирует `src/core/tl_schema_gen.cpp` (636 конструкторов). `core/tl_parser`
строит из ответа дерево `TlNode`/`TlValue`, поэтому парсер не ломается на
необязательных полях вроде медиа, реакций и премиум-флагов.

Перегенерация:

```sh
python3 tools/gen_tl.py
```
