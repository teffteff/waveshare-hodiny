#!/bin/zsh
# Sestaví na Macu rozdělanou práci z Beelinku a nahraje ji do hodin na USB.
#
# Claude na Beelinku upravuje kód a pouští tools/run_host_tests.sh; hodiny
# a rychlý překlad zůstávají u Macu. Skript stáhne pracovní strom z Beelinku
# i s necommitnutými změnami (rsync přes Tailscale) do ~/waveshare/hodiny-beelink,
# sestaví ho tam a nahraje. Tento klon zůstane netknutý. Hesla (.env,
# WaveshareHodiny/local/) se berou odsud odkazem a na Beelink nejdou.
#
#   tools/flash-from-beelink.sh [lcd21|lcd7 ...] [--work] [--no-upload]
#
# Bez desky sestaví a nahraje obě. Porty jsou ve WaveshareHodiny/local/devices.env:
#   LCD21_PORT=/dev/cu.usbmodem...   (kulaté hodiny)
#   LCD7_PORT=/dev/cu.usbmodem...    (7")
# BEELINK (výchozí home) a BEELINK_DIR (výchozí waveshare-hodiny) určí zdroj.
set -euo pipefail

ROOT_DIR="${0:A:h:h}"
BUILD_DIR="${HODINY_BEELINK_BUILD:-${ROOT_DIR:h}/hodiny-beelink}"
BEELINK="${BEELINK:-home}"
BEELINK_DIR="${BEELINK_DIR:-waveshare-hodiny}"
DEVICES_FILE="$ROOT_DIR/WaveshareHodiny/local/devices.env"

boards=()
wifi_profile=home
upload=1
for arg in "$@"; do
  case "$arg" in
    lcd21|lcd7) boards+=("$arg") ;;
    --work) wifi_profile=work ;;
    --no-upload) upload=0 ;;
    *) print -u2 "Neznámý argument: $arg"; exit 1 ;;
  esac
done
(( ${#boards} )) || boards=(lcd21 lcd7)

typeset -A ports
if (( upload )); then
  [[ -f "$DEVICES_FILE" ]] && source "$DEVICES_FILE"
  ports=(lcd21 "${LCD21_PORT:-}" lcd7 "${LCD7_PORT:-}")
  # Porty se ověří před překladem, ať se nečeká zbytečně.
  for board in $boards; do
    if [[ ! -e "${ports[$board]}" ]]; then
      print -u2 "Deska $board nemá port v $DEVICES_FILE nebo není připojená (${ports[$board]:-nezadáno})."
      print -u2 "Připojené: $(ls /dev/cu.usbmodem* 2>/dev/null | tr '\n' ' ')"
      exit 1
    fi
  done
fi

print "Beelink $BEELINK:$BEELINK_DIR"
ssh "$BEELINK" "cd $BEELINK_DIR && git log -1 --format='  %h %s (%cr)' && git status --short | sed 's/^/  /'"

# Vynechané cesty rsync nemaže, takže odkazy níž přežijí i --delete.
mkdir -p "$BUILD_DIR"
rsync -a --delete \
  --exclude=/.git --exclude=/.arduino --exclude=/build --exclude=/.env \
  --exclude=/WaveshareHodiny/local --exclude='/WaveshareHodiny/secrets*.h' \
  --exclude=/WaveshareHodiny/firmware_config.h --exclude=/WaveshareHodiny/release_version.h \
  --exclude=/WaveshareHodiny/CompressedPages.h --exclude=__pycache__ --exclude=.DS_Store \
  "$BEELINK:$BEELINK_DIR/" "$BUILD_DIR/"

ln -sfn "$ROOT_DIR/WaveshareHodiny/local" "$BUILD_DIR/WaveshareHodiny/local"
[[ -f "$ROOT_DIR/.env" ]] && ln -sfn "$ROOT_DIR/.env" "$BUILD_DIR/.env"
# Sestavení má vlastní .arduino (arduino-cli při změně cesty ke sketchi překládá
# všechno znovu), SDK pro 7" a python s pyserial se sdílí.
mkdir -p "$BUILD_DIR/.arduino"
for shared in python sdk-lcd7 sdk-lcd7-dl; do
  [[ -e "$ROOT_DIR/.arduino/$shared" ]] && ln -sfn "$ROOT_DIR/.arduino/$shared" "$BUILD_DIR/.arduino/$shared"
done

for board in $boards; do
  print "\n== $board: sestavení"
  BOARD=$board "$BUILD_DIR/build.sh" "$wifi_profile"
  if (( upload )); then
    print "\n== $board: nahrání na ${ports[$board]}"
    BOARD=$board "$BUILD_DIR/upload.sh" "${ports[$board]}"
  fi
done
