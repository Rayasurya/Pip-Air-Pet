// Copy this file to secrets.h (same folder) and fill it in. secrets.h is git-ignored.
#pragma once

// Wi-Fi: gives Pip the clock time (needed for the forecast model and for Kin).
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"
// Password for uploading new firmware over Wi-Fi. Make one up.
#define OTA_PASSWORD "make-one-up"

// Kin (optional): who Pip watches over, and where the family messages go.
#define PARENT_NAME "Mom"
// Telegram: make a bot with @BotFather to get a token; message the bot once from the family
// phone, then get your chat id (see kin/README.md). Leave these out to disable messages.
// #define TELEGRAM_BOT_TOKEN "123456:ABC..."
// #define TELEGRAM_CHAT_ID "123456789"
// Optional but recommended: the root certificate for api.telegram.org, so the connection is
// verified (see kin/README.md for how to get it). Without it Pip still sends, unverified.
// #define TELEGRAM_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// Body diary (optional): your city, for outdoor air pressure. 1 decimal place is enough and
// keeps your exact location private. Defaults to Bengaluru.
// #define HOME_LAT 12.9f
// #define HOME_LON 77.6f
// Share "not feeling well" presses in the family's evening note (only if the person agrees).
// #define SHARE_MARKS_WITH_FAMILY 1
