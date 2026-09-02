#!/usr/bin/env bash
# Přeloží a spustí všechny testy běžící na počítači. Nevyžaduje arduino-cli
# ani připojené hodiny, takže je to nejrychlejší kontrola před sestavením.
set -uo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/WaveshareHodiny"
SHIM_DIR="$ROOT_DIR/tools/hostshim"
BUILD_DIR="${TMPDIR:-/tmp}/waveshare-hodiny-host-tests"
CXX_BIN="${CXX:-c++}"

mkdir -p "$BUILD_DIR"

# Název testu -> zdrojové soubory firmwaru, které potřebuje k sestavení.
run_test() {
  local name="$1"
  shift
  local sources=("$ROOT_DIR/tools/test_$name.cpp" "$@")
  printf '%-24s ' "$name"
  if ! "$CXX_BIN" -std=c++17 -Wall -I "$SHIM_DIR" -I "$FIRMWARE_DIR" \
      -o "$BUILD_DIR/$name" "${sources[@]}" > "$BUILD_DIR/$name.log" 2>&1; then
    echo "PŘEKLAD SELHAL"
    sed 's/^/    /' "$BUILD_DIR/$name.log"
    return 1
  fi
  if ! "$BUILD_DIR/$name" > "$BUILD_DIR/$name.out" 2>&1; then
    echo "SELHAL"
    sed 's/^/    /' "$BUILD_DIR/$name.out"
    return 1
  fi
  echo "OK"
  return 0
}

failures=0
run_test day_night_logic "$FIRMWARE_DIR/DayNightLogic.cpp" || failures=$((failures + 1))
run_test semver "$FIRMWARE_DIR/SemVer.cpp" || failures=$((failures + 1))
run_test tmep_parser "$FIRMWARE_DIR/TmepParser.cpp" || failures=$((failures + 1))
run_test home_assistant_connection_policy || failures=$((failures + 1))
run_test clock_config "$FIRMWARE_DIR/ClockConfig.cpp" || failures=$((failures + 1))

echo
if [[ $failures -gt 0 ]]; then
  echo "Selhalo testů: $failures"
  exit 1
fi
echo "Všechny testy prošly."
