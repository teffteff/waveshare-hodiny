#!/usr/bin/env bash
# Připraví pro 7" desku Arduino core esp32 3.0.7 s knihovnami ESP-IDF
# "esp32-3.0.7-h" od Espressifu (datová cache s 64B řádky, kód z PSRAM).
# Se standardními knihovnami běží kód z flash, která sdílí sběrnici s PSRAM;
# při práci radaru pak přerušení RGB panelu nestíhá a obraz se posouvá.
# Ověřeno s běžícím radarem: změna rozsahu 12 posunů -> 0. Daň: kód a
# konstanty (~3 MB) leží v PSRAM, proto má radar 7" poloviční rozlišení.
#
# Vznikne datový adresář arduino-cli v .arduino/sdk-lcd7: hardware je kopie
# stock core, nástroje (kompilátory, esptool) jsou odkazy do stock adresáře
# a vlastní jsou jen esp32-arduino-libs. build.sh ho pro BOARD=lcd7 použije
# sám. Kulatý 2,1" se dál překládá se standardními knihovnami.
#
# ZIP se stahuje napřed ze zrcadla v GitHub release tohoto repozitáře
# (adresa na dl.espressif.com nemá verzi a soubor pod ní se může změnit nebo
# zmizet), teprve potom od Espressifu; v obou případech se ověří SHA-256.
# Stažený ZIP zůstane v .arduino/sdk-lcd7-dl, aby šel nahrát do zrcadla.
#
# Značka .ready nese verzi core, SHA-256 ZIPu a stock adresář; když se
# kterákoli liší (nová verze skriptu, jiný core), SDK se připraví znovu.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="$ROOT_DIR/.arduino/sdk-lcd7"
DOWNLOAD_DIR="${LCD7_SDK_DOWNLOAD_DIR:-$ROOT_DIR/.arduino/sdk-lcd7-dl}"
CORE_VERSION="3.0.7"
ZIP_NAME="esp32-3.0.7-h.zip"
ZIP_SHA256="0778383df57b7f34730bdb234f62c93b2bfadc141d082d522b56964e9e23be99"
LIBS_VERSION="idf-release_v5.1-632e0c2a"
LIBS_UNPACKED_DIR="arduino-esp32-libs-all-release_v5.1-632e0c2a9f"
# Zrcadlo: asset release "sdk-lcd7-3.0.7-h" v tomto repozitáři. Tag nese verzi,
# takže se obsah pod adresou nezmění; SHA-256 je stejně povinná.
MIRROR_URL="${LCD7_SDK_MIRROR_URL:-https://github.com/teffteff/waveshare-hodiny/releases/download/sdk-lcd7-3.0.7-h/$ZIP_NAME}"
ESPRESSIF_URL="https://dl.espressif.com/AE/esp-arduino-libs/$ZIP_NAME"

STOCK_DIR="${ARDUINO_STOCK_DATA_DIR:-$(arduino-cli config get directories.data)}"
STOCK_DIR="$(cd -- "$STOCK_DIR" && pwd)"
STOCK_CORE="$STOCK_DIR/packages/esp32/hardware/esp32/$CORE_VERSION"
STOCK_LIBS="$STOCK_DIR/packages/esp32/tools/esp32-arduino-libs/$LIBS_VERSION"
if [[ ! -d "$STOCK_CORE" || ! -d "$STOCK_LIBS" ]]; then
  echo "Chybí core esp32 $CORE_VERSION v $STOCK_DIR (arduino-cli core install esp32:esp32@$CORE_VERSION)." >&2
  exit 1
fi

MARKER="core=$CORE_VERSION
libs=$LIBS_VERSION
zip_sha256=$ZIP_SHA256
stock=$STOCK_DIR"

sdk_is_ready() {
  [[ -f "$SDK_DIR/.ready" && "$(cat "$SDK_DIR/.ready")" == "$MARKER" ]] || return 1
  # Odkazy na nástroje míří do stock adresáře; po jeho smazání SDK nefunguje.
  local tool
  for tool in "$SDK_DIR"/packages/esp32/tools/*; do
    [[ -e "$tool" ]] || return 1
  done
  [[ -f "$SDK_DIR/packages/esp32/tools/esp32-arduino-libs/$LIBS_VERSION/esp32s3/sdkconfig" ]]
}

if sdk_is_ready; then
  echo "SDK pro 7\" už je připravené: $SDK_DIR"
  exit 0
fi

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

ZIP="$DOWNLOAD_DIR/$ZIP_NAME"
mkdir -p "$DOWNLOAD_DIR"
if [[ -f "$ZIP" && "$(sha256_of "$ZIP")" != "$ZIP_SHA256" ]]; then
  echo "ZIP v $DOWNLOAD_DIR nesouhlasí se SHA-256, stáhne se znovu." >&2
  rm -f "$ZIP"
fi
if [[ ! -f "$ZIP" ]]; then
  for url in "$MIRROR_URL" "$ESPRESSIF_URL"; do
    echo "Stahuji $url" >&2
    rm -f "$ZIP.part"
    if ! curl -fsSL --retry 3 --connect-timeout 30 -o "$ZIP.part" "$url"; then
      echo "Stažení selhalo: $url" >&2
      continue
    fi
    actual="$(sha256_of "$ZIP.part")"
    if [[ "$actual" != "$ZIP_SHA256" ]]; then
      echo "SHA-256 nesouhlasí ($url): $actual, čekáno $ZIP_SHA256" >&2
      continue
    fi
    mv "$ZIP.part" "$ZIP"
    break
  done
  rm -f "$ZIP.part"
  if [[ ! -f "$ZIP" ]]; then
    echo "Knihovny pro 7\" se nepodařilo stáhnout ani ze zrcadla, ani od Espressifu." >&2
    exit 1
  fi
fi

rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR/packages/esp32/hardware" "$SDK_DIR/packages/esp32/tools/esp32-arduino-libs"
# Hardware (47 MB) je kopie; na APFS je cp -c klon bez kopírování dat.
cp -cR "$STOCK_DIR/packages/esp32/hardware/esp32" "$SDK_DIR/packages/esp32/hardware/" 2>/dev/null ||
  cp -R "$STOCK_DIR/packages/esp32/hardware/esp32" "$SDK_DIR/packages/esp32/hardware/"
# Nástroje (přes 2 GB) jsou jen odkazy: kopie by v CI zdvojnásobila cache.
for tool in "$STOCK_DIR"/packages/esp32/tools/*; do
  name="$(basename -- "$tool")"
  [[ "$name" == esp32-arduino-libs ]] && continue
  ln -s "$tool" "$SDK_DIR/packages/esp32/tools/$name"
done
for index in "$STOCK_DIR"/*.json "$STOCK_DIR"/*.json.sig; do
  [[ -e "$index" ]] && ln -sf "$index" "$SDK_DIR/"
done

UNPACK="$SDK_DIR/unpack"
unzip -q "$ZIP" -d "$UNPACK"
LIBS="$SDK_DIR/packages/esp32/tools/esp32-arduino-libs/$LIBS_VERSION"
mv "$UNPACK/$LIBS_UNPACKED_DIR" "$LIBS"
rm -rf "$UNPACK"
grep -q '^CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y' "$LIBS/esp32s3/sdkconfig"
printf '%s' "$MARKER" > "$SDK_DIR/.ready"
echo "SDK pro 7\" připraveno: $SDK_DIR"
