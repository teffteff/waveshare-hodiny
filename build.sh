#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WIFI_PROFILE="${1:-home}"
if [[ "$WIFI_PROFILE" != "home" && "$WIFI_PROFILE" != "work" ]]; then
  echo "Použití: ./build.sh [home|work]" >&2
  exit 1
fi
# BOARD=lcd7 přeloží firmware pro ESP32-S3-Touch-LCD-7, jinak pro kulatý 2,1".
BOARD="${BOARD:-lcd21}"
case "$BOARD" in
  lcd21) BOARD_SUFFIX=""; BOARD_FLAGS="" ;;
  lcd7) BOARD_SUFFIX="-lcd7"; BOARD_FLAGS=" -DHODINY_BOARD_LCD7=1" ;;
  *) echo "Neznámá deska: $BOARD (lcd21 nebo lcd7)" >&2; exit 1 ;;
esac
if [[ "$BOARD" == "lcd7" ]]; then
  # 7" potřebuje knihovny ESP-IDF, které spouštějí kód z PSRAM: jinak sdílí
  # přerušení RGB panelu sběrnici s flash a při práci radaru obraz ujíždí
  # (viz tools/setup_lcd7_sdk.sh).
  "$ROOT_DIR/tools/setup_lcd7_sdk.sh" >/dev/null
  export ARDUINO_DIRECTORIES_DATA="$ROOT_DIR/.arduino/sdk-lcd7"
fi
BUILD_PATH="$ROOT_DIR/.arduino/build-waveshare-hodiny-develop$BOARD_SUFFIX"
OUTPUT_DIR="$ROOT_DIR/build/waveshare-hodiny-develop$BOARD_SUFFIX"
ARDUINO_CLI_BIN="${ARDUINO_CLI_BIN:-$(command -v arduino-cli || true)}"
PYTHON_BIN="${PYTHON_BIN:-$(command -v python3 || true)}"
if [[ -z "$ARDUINO_CLI_BIN" || -z "$PYTHON_BIN" ]]; then
  echo "Chybí arduino-cli nebo python3 v PATH." >&2
  exit 1
fi
ARDUINO_CONFIG_FILE="${ARDUINO_CONFIG_FILE:-$ROOT_DIR/WaveshareHodiny/local/arduino-cli.yaml}"
if [[ ! -f "$ARDUINO_CONFIG_FILE" ]]; then
  ARDUINO_CONFIG_FILE="$ROOT_DIR/arduino-cli.yaml"
fi
if [[ "$WIFI_PROFILE" == "work" ]]; then
  "$PYTHON_BIN" "$ROOT_DIR/tools/generate_secrets.py" --work-wifi
else
  "$PYTHON_BIN" "$ROOT_DIR/tools/generate_secrets.py"
fi
"$PYTHON_BIN" "$ROOT_DIR/tools/validate_weather_icon_parity.py"
"$PYTHON_BIN" "$ROOT_DIR/tools/generate_compressed_pages.py"
"$ARDUINO_CLI_BIN" \
  --config-file "$ARDUINO_CONFIG_FILE" \
  compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default \
  --build-property "compiler.c.extra_flags=-MMD -c -DLV_CONF_PATH=ClockLvglConfig.h$BOARD_FLAGS" \
  --build-property "compiler.cpp.extra_flags=-MMD -c -DLV_CONF_PATH=ClockLvglConfig.h -DWAVESHARE_DEVELOPMENT_BUILD=1$BOARD_FLAGS" \
  --build-property 'upload.maximum_size=6291456' \
  --build-path "$BUILD_PATH" \
  --output-dir "$OUTPUT_DIR" \
  "$ROOT_DIR/WaveshareHodiny"
