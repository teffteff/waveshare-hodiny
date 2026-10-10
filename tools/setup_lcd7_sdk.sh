#!/usr/bin/env bash
# Připraví pro 7" desku Arduino core esp32 3.0.7 s knihovnami ESP-IDF
# "esp32-3.0.7-h" od Espressifu (datová cache s 64B řádky, kód z PSRAM).
# Se standardními knihovnami RGB panel nestíhal číst framebuffer z PSRAM,
# když radar na druhém jádře dekódoval snímky, a obraz se posouval.
#
# Vznikne kopie datového adresáře arduino-cli v .arduino/sdk-lcd7, ve které
# se vymění jen esp32-arduino-libs; build.sh ji pro BOARD=lcd7 použije sám.
# Kulatý 2,1" se dál překládá se standardními knihovnami.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="$ROOT_DIR/.arduino/sdk-lcd7"
ZIP_URL="https://dl.espressif.com/AE/esp-arduino-libs/esp32-3.0.7-h.zip"
ZIP_SHA256="0778383df57b7f34730bdb234f62c93b2bfadc141d082d522b56964e9e23be99"
LIBS_VERSION="idf-release_v5.1-632e0c2a"

STOCK_DIR="${ARDUINO_STOCK_DATA_DIR:-$(arduino-cli config get directories.data)}"
STOCK_CORE="$STOCK_DIR/packages/esp32/hardware/esp32/3.0.7"
STOCK_LIBS="$STOCK_DIR/packages/esp32/tools/esp32-arduino-libs/$LIBS_VERSION"
if [[ ! -d "$STOCK_CORE" || ! -d "$STOCK_LIBS" ]]; then
  echo "Chybí core esp32 3.0.7 v $STOCK_DIR (arduino-cli core install esp32:esp32@3.0.7)." >&2
  exit 1
fi

if [[ -f "$SDK_DIR/.ready" ]]; then
  echo "SDK pro 7\" už je připravené: $SDK_DIR"
  exit 0
fi

rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR/packages" "$SDK_DIR/dl"
# Na APFS je cp -c klon bez kopírování dat.
cp -cR "$STOCK_DIR/packages/esp32" "$SDK_DIR/packages/" 2>/dev/null ||
  cp -R "$STOCK_DIR/packages/esp32" "$SDK_DIR/packages/"
for index in "$STOCK_DIR"/*.json "$STOCK_DIR"/*.json.sig; do
  [[ -e "$index" ]] && ln -sf "$index" "$SDK_DIR/"
done

ZIP="$SDK_DIR/dl/esp32-3.0.7-h.zip"
curl -fsSL --retry 3 -o "$ZIP" "$ZIP_URL"
echo "$ZIP_SHA256  $ZIP" | shasum -a 256 -c -

UNPACK="$SDK_DIR/dl/unpack"
unzip -q "$ZIP" -d "$UNPACK"
LIBS="$SDK_DIR/packages/esp32/tools/esp32-arduino-libs/$LIBS_VERSION"
rm -rf "$LIBS"
mv "$UNPACK"/arduino-esp32-libs-all-release_v5.1-632e0c2a9f "$LIBS"
rm -rf "$UNPACK" "$ZIP"
grep -q '^CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y' "$LIBS/esp32s3/sdkconfig"
touch "$SDK_DIR/.ready"
echo "SDK pro 7\" připraveno: $SDK_DIR"
