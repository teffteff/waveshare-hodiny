#!/usr/bin/env bash
# Kouřový test balení bez arduino-cli: z umělých obrazů obou desek složí
# balíčky (prepare_release_package.py), web s firmwarem a katalogem verzí
# jako pages.yml a ověří ho validate_pages_bundle.py. Ověří i to, že balíček
# s obrazem jiné desky nebo bez značky desky neprojde.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/hodiny-release-package.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
VERSION="0.0.0"

# Umělý obraz: náhodná data se značkou desky uprostřed (jako .rodata).
make_image() {
  local path="$1" tag="$2"
  {
    head -c 70000 /dev/urandom
    [[ -n "$tag" ]] && printf 'HODINY_BOARD_TAG=%s;' "$tag"
    printf 'HODINY_BOARD_TAG=\0'
    head -c 50000 /dev/urandom
  } > "$path"
}

head -c 20000 /dev/urandom > "$WORK/bootloader.bin"
head -c 3072 /dev/urandom > "$WORK/partitions.bin"
head -c 8192 /dev/urandom > "$WORK/boot_app0.bin"
make_image "$WORK/app-lcd21.bin" lcd21
make_image "$WORK/app-lcd7.bin" lcd7
make_image "$WORK/app-none.bin" ""

package() {
  local board="$1" app="$2" out="$3"
  RELEASE_BOARD="$board" "$PYTHON_BIN" "$ROOT_DIR/tools/prepare_release_package.py" \
    "$VERSION" "$WORK/bootloader.bin" "$WORK/partitions.bin" "$WORK/boot_app0.bin" \
    "$app" "$app" "$out" /waveshare-hodiny/firmware
}

failures=0
expect_failure() {
  local description="$1"
  shift
  if "$@" > "$WORK/expected-failure.log" 2>&1; then
    echo "CHYBA: $description prošlo, mělo selhat." >&2
    failures=$((failures + 1))
  else
    echo "OK: $description odmítnuto ($(tail -n 1 "$WORK/expected-failure.log"))"
  fi
}

package lcd21 "$WORK/app-lcd21.bin" "$WORK/pkg-lcd21" > /dev/null
package lcd7 "$WORK/app-lcd7.bin" "$WORK/pkg-lcd7" > /dev/null
expect_failure "7\" balíček s obrazem 2,1\"" package lcd7 "$WORK/app-lcd21.bin" "$WORK/bad"
expect_failure "2,1\" balíček s obrazem 7\"" package lcd21 "$WORK/app-lcd7.bin" "$WORK/bad"
expect_failure "balíček s obrazem bez značky desky" package lcd21 "$WORK/app-none.bin" "$WORK/bad"

# Web jako po nasazení: aktuální verze v firmware/ a archiv v releases/.
SITE="$WORK/site"
"$PYTHON_BIN" "$ROOT_DIR/tools/prepare_pages_site.py" "$SITE"
release_dir="$SITE/firmware/releases/$VERSION"
mkdir -p "$release_dir"
for board in lcd21 lcd7; do
  cp "$WORK/pkg-$board"/manifest*.json "$WORK/pkg-$board"/ota*.json \
    "$WORK/pkg-$board"/*.bin "$SITE/firmware/"
  cp "$WORK/pkg-$board"/manifest*.json "$release_dir/"
  for part in bootloader partitions boot-app0 app; do
    cp "$WORK/pkg-$board"/waveshare-hodiny*."$part".bin "$release_dir/"
  done
done
cat > "$SITE/firmware/releases.json" <<EOF
{"releases": [{"version": "$VERSION", "manifest": "firmware/releases/$VERSION/manifest.json", "manifestLcd7": "firmware/releases/$VERSION/manifest-lcd7.json"}]}
EOF
"$PYTHON_BIN" "$ROOT_DIR/tools/validate_pages_bundle.py" "$SITE"

# Rozbitý web musí validace odmítnout: 7" metadata ukazující na obraz 2,1".
sed 's/waveshare-hodiny-lcd7\.ota\.bin/waveshare-hodiny.ota.bin/' \
  "$SITE/firmware/ota-lcd7.json" > "$WORK/ota-lcd7.json"
cp "$WORK/ota-lcd7.json" "$SITE/firmware/ota-lcd7.json"
expect_failure "web se 7\" metadaty na obraz 2,1\"" \
  "$PYTHON_BIN" "$ROOT_DIR/tools/validate_pages_bundle.py" "$SITE"

if [[ $failures -gt 0 ]]; then
  exit 1
fi
echo "Balení obou desek i validace webu fungují."
