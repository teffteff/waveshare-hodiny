#!/usr/bin/env bash
# Převede poslední pád hodin z diagnostiky na řádky zdrojáku.
#
#   tools/decode-crash.sh barvlevo              # ELF stáhne z GitHub Release
#   tools/decode-crash.sh barvlevo build/x.elf  # vlastní sestavení
#
# Hodiny po pádu uloží souhrn výpisu (CrashLog.cpp) a /api/diagnostics ho
# vrací bez přihlášení. Adresy dávají smysl jen proti ELF téhož sestavení,
# takže skript porovná začátek jeho SHA-256 s elfSha z hodin.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${1:?Pouziti: tools/decode-crash.sh HODINY [ELF]}"
ELF="${2:-}"
[[ "$HOST" == *.* ]] || HOST="$HOST.local"
REPO="${DECODE_CRASH_REPO:-teffteff/waveshare-hodiny}"

ADDR2LINE="${ADDR2LINE:-$(ls ~/Library/Arduino15/packages/esp32/tools/esp-xs3/*/bin/xtensa-esp32s3-elf-addr2line \
  ~/.arduino15/packages/esp32/tools/esp-xs3/*/bin/xtensa-esp32s3-elf-addr2line 2>/dev/null | tail -n 1 || true)}"
if [[ -z "$ADDR2LINE" || ! -x "$ADDR2LINE" ]]; then
  echo "Chybí xtensa-esp32s3-elf-addr2line (nastav ADDR2LINE)." >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsS -m 15 "http://$HOST/api/diagnostics" -o "$WORK/diagnostics.json"

# Vypíše: firmware, elfSha a adresy (pc, pak zpětná stopa), každé na řádek.
status=0
python3 - "$WORK/diagnostics.json" > "$WORK/crash.txt" <<'EOF' || status=$?
import json, sys
diagnostics = json.load(open(sys.argv[1]))
if "crashes" not in diagnostics:
    print(f"Firmware {diagnostics.get('firmwareVersion')} pady jeste nezaznamenava "
          f"(posledni reset {diagnostics.get('resetReason')}).", file=sys.stderr)
    sys.exit(3)
crashes = diagnostics["crashes"] or {}
last = crashes.get("last")
reasons = {3: "softwarovy restart", 4: "panika", 5: "watchdog preruseni",
           6: "watchdog ulohy", 7: "jiny watchdog", 9: "podpeti"}
print(f"Pady celkem: {crashes.get('count', 0)}", file=sys.stderr)
if not last:
    print("Hodiny zadny pad nezaznamenaly.", file=sys.stderr)
    sys.exit(3)
reason = last.get("resetReason")
print(f"Firmware {last.get('firmware')}, start po resetu {reason} "
      f"({reasons.get(reason, '?')})", file=sys.stderr)
if not last.get("dump"):
    print("Bez vypisu pameti (watchdog nebo podpeti), neni co dekodovat.",
          file=sys.stderr)
    sys.exit(3)
print(f"Uloha {last.get('task')!r}: {last.get('reason') or '(bez duvodu)'}, "
      f"exccause {last.get('exceptionCause')}, excvaddr {last.get('exceptionAddress')}"
      + (", zpetna stopa POSKOZENA" if last.get("backtraceCorrupted") else ""),
      file=sys.stderr)
print(last.get("firmware", ""))
print(last.get("elfSha", ""))
print(last.get("pc", ""))
for address in last.get("backtrace", []):
    print(address)
EOF
# 3 = není co dekódovat (žádný pád nebo pád bez výpisu); to není chyba.
[[ $status -eq 3 ]] && exit 0
[[ $status -eq 0 ]] || exit $status

FIRMWARE="$(sed -n 1p "$WORK/crash.txt")"
ELF_SHA="$(sed -n 2p "$WORK/crash.txt")"
if [[ -z "$ELF" ]]; then
  # Pád při OTA restartu patří předchozímu firmwaru, takže verze v záznamu
  # (a od 2.2.16 prázdná verze) nemusí ukazovat na správný ELF. Zkusí se
  # nejdřív ta, pak poslední releasy, a platí ten, jehož SHA sedí.
  CANDIDATES=()
  [[ -n "$FIRMWARE" ]] && CANDIDATES+=("v$FIRMWARE")
  while IFS= read -r tag; do
    [[ "$tag" == "v$FIRMWARE" ]] || CANDIDATES+=("$tag")
  done < <(curl -fsSL -m 30 "https://api.github.com/repos/$REPO/releases?per_page=15" \
             | python3 -c 'import json,sys; [print(r["tag_name"]) for r in json.load(sys.stdin)]' \
             2>/dev/null || true)
  for tag in "${CANDIDATES[@]}"; do
    echo "Zkouším ELF z $tag..." >&2
    curl -fsSL -m 300 -o "$WORK/candidate.elf" \
      "https://github.com/$REPO/releases/download/$tag/waveshare-hodiny.elf" 2>/dev/null || continue
    if [[ -z "$ELF_SHA" || "$(shasum -a 256 "$WORK/candidate.elf" | cut -c1-${#ELF_SHA})" == "$ELF_SHA" ]]; then
      ELF="$WORK/waveshare-hodiny.elf"
      mv "$WORK/candidate.elf" "$ELF"
      echo "Pád je ze sestavení $tag." >&2
      break
    fi
  done
  if [[ -z "$ELF" ]]; then
    echo "Žádný z posledních releasů nemá ELF se SHA $ELF_SHA. Předej ho jako druhý argument." >&2
    exit 1
  fi
fi

ACTUAL_SHA="$(shasum -a 256 "$ELF" | cut -c1-${#ELF_SHA})"
if [[ -n "$ELF_SHA" && "$ACTUAL_SHA" != "$ELF_SHA" ]]; then
  echo "ELF nesedí: hodiny spadly v sestavení $ELF_SHA, soubor je $ACTUAL_SHA." >&2
  echo "Adresy by ukazovaly na jiné řádky, takže dekódování končí." >&2
  exit 1
fi

echo >&2
# Na Xtensa ukazuje adresa ve stopě za volání; addr2line ukáže řádek volání.
tail -n +3 "$WORK/crash.txt" | xargs "$ADDR2LINE" -pfiaC -e "$ELF" \
  | sed "s|$ROOT_DIR/||g"
