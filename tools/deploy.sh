#!/usr/bin/env bash
# Nasadí služby z infra/ na server podle infra/manifest.txt.
#
#   tools/deploy.sh rain               jedna služba
#   tools/deploy.sh news agenda        víc služeb
#   tools/deploy.sh --all              všechno; co se nezměnilo, se nerestartuje
#   tools/deploy.sh --init alerts      první zavedení: i uživatel a adresáře
#   tools/deploy.sh --list             jen vypíše služby z manifestu
#   tools/deploy.sh --hash RAIN        heslo RAIN_PASSWORD z .env → RAIN_HASH
#                                      v /etc/caddy/caddy.env, pak restart Caddy
#
# Přepínače k nasazení:
#   --force        restartovat i služby, kterým se žádný soubor nezměnil
#   --allow-dirty  nasadit i necommitnuté změny v infra/ (jinak odmítne)
#   --no-check     na konci nespouštět tools/check-stack.sh --deep
#   --dry-run      jen vypsat skript, který by běžel na serveru; nic nenahraje
#
# Co udělá: nahraje soubory do ~/deploy-stage na serveru, nainstaluje je jako
# root (kód patří rootovi, viz „Zabezpečení stroje“ v infra/README.md),
# daemon-reload, a u služeb, kterým se něco změnilo, provede akce z manifestu
# (restart, start, enable, run). Nový Caddyfile dostane reload; když reload
# spadne, vrátí se předchozí Caddyfile, aby na disku neležela konfigurace,
# která by shodila příští restart. Nakonec ověří otisky a pustí
# tools/check-stack.sh --deep.
#
# Návratový kód 0 = hotovo, 1 = chyba.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
. "$REPO_ROOT/tools/manifest-lib.sh"

INIT=0; FORCE=0; DIRTY=0; CHECK=1; ALL=0; DRY=0; HASH_NAME=""
SERVICES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --init) INIT=1 ;;
    --force) FORCE=1 ;;
    --allow-dirty) DIRTY=1 ;;
    --no-check) CHECK=0 ;;
    --dry-run) DRY=1 ;;
    --all) ALL=1 ;;
    --list) manifest_services; exit 0 ;;
    --hash) shift; HASH_NAME="${1:-}"; [ -n "$HASH_NAME" ] || { echo "--hash chce jméno, např. RAIN" >&2; exit 1; } ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    -*) echo "Neznámý přepínač: $1" >&2; exit 1 ;;
    *) SERVICES+=("$1") ;;
  esac
  shift
done

if [ -f .env ]; then
  set -a; . ./.env; set +a
fi
: "${CLOCK_HOST:?Chybí CLOCK_HOST v .env}"
: "${CLOCK_SSH:?Chybí CLOCK_SSH v .env}"
: "${CLOCK_SSH_KEY:?Chybí CLOCK_SSH_KEY v .env}"

SSH=(ssh -i "$CLOCK_SSH_KEY" -o ConnectTimeout=15
     -o BatchMode=yes -o LogLevel=ERROR "$CLOCK_SSH")

step() { printf '\n== %s\n' "$1"; }
die()  { printf 'CHYBA: %s\n' "$1" >&2; exit 1; }

# --- heslo do Caddy -------------------------------------------------------------
if [ -n "$HASH_NAME" ]; then
  var="${HASH_NAME}_PASSWORD"
  password="${!var:-}"
  [ -n "$password" ] || die "v .env chybí $var (vyrob: openssl rand -hex 24)"
  case "$password" in *[!0-9a-zA-Z]*) die "$var smí mít jen písmena a číslice (jde do adresy a do .env)" ;; esac
  step "Hash hesla $var"
  hash="$("${SSH[@]}" "caddy hash-password --plaintext '$password'")"
  case "$hash" in '$2a$'*) ;; *) die "caddy hash-password nevrátil bcrypt hash" ;; esac
  # Jen tenhle řádek: caddy.env nese i ostatní *_HASH a bez nich by Caddy
  # nenastartoval. Úprava přes dočasný soubor zachová práva 600.
  "${SSH[@]}" "sudo bash -s" <<EOF
set -euo pipefail
f=/etc/caddy/caddy.env
tmp=\$(mktemp /etc/caddy/.caddy.env.XXXXXX)
grep -v '^${HASH_NAME}_HASH=' "\$f" > "\$tmp" || true
printf '%s=%s\n' '${HASH_NAME}_HASH' '$hash' >> "\$tmp"
chmod 600 "\$tmp"; chown root:root "\$tmp"
mv "\$tmp" "\$f"
restorecon "\$f" 2>/dev/null || true
EOF
  step "Restart Caddy"
  # Restart, ne reload: proměnné z EnvironmentFile dostane jen nový proces.
  "${SSH[@]}" "sudo systemctl restart caddy && systemctl is-active caddy" \
    || die "Caddy po restartu neběží — journalctl -u caddy"
  echo "${HASH_NAME}_HASH zapsaný, Caddy běží"
  exit 0
fi

# --- výběr služeb -----------------------------------------------------------------
if [ $ALL -eq 1 ]; then
  SERVICES=()
  for s in $(manifest_services); do SERVICES+=("$s"); done
fi
[ ${#SERVICES[@]} -gt 0 ] || { sed -n '2,25p' "$0"; exit 1; }
known=" $(manifest_services | tr '\n' ' ') "
for s in "${SERVICES[@]}"; do
  [ "${known#* $s }" != "$known" ] || die "služba '$s' není v infra/manifest.txt (tools/deploy.sh --list)"
done

if [ $DRY -eq 0 ] && [ $DIRTY -eq 0 ] && [ -n "$(git status --porcelain -- infra)" ]; then
  git status --short -- infra
  die "necommitnuté změny v infra/ — commitni, nebo --allow-dirty"
fi

# --- skript pro server --------------------------------------------------------------
if [ $DRY -eq 1 ]; then
  STAGE="~/deploy-stage"
else
  STAGE="$("${SSH[@]}" 'echo $HOME')/deploy-stage"
fi
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
q() { printf '%q' "$1"; }
script="$stage/remote.sh"
{
  echo 'set -euo pipefail'
  echo "S=$(q "$STAGE")"
  echo 'caddy_new=0'
  # install jen při změně; vrací 0, když se soubor změnil
  cat <<'EOF'
put() {  # put <mód> <zdroj> <cíl>
  if [ -f "$3" ] && cmp -s "$2" "$3"; then return 1; fi
  # exit, ne return: selhaná instalace nesmí vypadat jako "beze změny" (1).
  install -o root -g root -m "$1" "$2" "$3" || exit 1
  echo "  nový: $3"
}
EOF
  for s in "${SERVICES[@]}"; do echo "changed_$s=$FORCE$INIT"; done

  if [ $INIT -eq 1 ]; then
    manifest_lines "${SERVICES[@]}" | while read -r svc kind a b c; do
      case "$kind" in
        user) echo "id $(q "$a") >/dev/null 2>&1 || useradd --system --no-create-home --home-dir $(q "$b") --shell /sbin/nologin $(q "$a")" ;;
        dir)  echo "install -d -o $(q "${b%%:*}") -g $(q "${b##*:}") -m $(q "$c") $(q "$a")" ;;
        secret) echo "[ -e $(q "$a") ] || { install -o $(q "${b%%:*}") -g $(q "${b##*:}") -m $(q "$c") /dev/null $(q "$a"); echo '  prázdný: $a — doplnit'; }" ;;
      esac
    done
  fi

  manifest_lines "${SERVICES[@]}" | while read -r svc kind a b; do
    case "$kind" in
      file)    echo "put 644 \"\$S\"/$(q "$a") $(q "$b") && changed_$svc=1 || true" ;;
      exec)    echo "put 700 \"\$S\"/$(q "$a") $(q "$b") && changed_$svc=1 || true" ;;
      sysunit) echo "put 644 \"\$S\"/$(q "$a") $(q "$b") && changed_$svc=1 || true" ;;
      unit)    echo "put 644 \"\$S\"/$(q "$a") $(q "$b") && changed_$svc=1 || true"
               echo "put 644 \"\$S\"/$(q "$a") /etc/systemd/system/$(q "$(basename "$b")") >/dev/null || true" ;;
      caddyfile)
               echo "cp -a /etc/caddy/Caddyfile /etc/caddy/Caddyfile.prev"
               echo "put 644 \"\$S\"/$(q "$a") $(q "$b") && caddy_new=1 || true" ;;
      dropin)  echo "install -d -m 755 /etc/systemd/system/$(q "$a").d"
               echo "put 644 \"\$S\"/$DROPIN_SOURCE /etc/systemd/system/$(q "$a").d/on-failure.conf || true" ;;
    esac
  done

  echo 'systemctl daemon-reload'

  cat <<'EOF'
if [ "$caddy_new" = 1 ]; then
  if systemctl reload caddy; then
    echo "  caddy: reload"
  else
    cp -a /etc/caddy/Caddyfile.prev /etc/caddy/Caddyfile
    echo "  caddy: reload SPADL, vrácen předchozí Caddyfile." >&2
    echo "  Chybí nový *_HASH v caddy.env? Nejdřív tools/deploy.sh --hash JMÉNO (restartuje Caddy)." >&2
    journalctl -u caddy -n 5 --no-pager -o cat >&2 || true
    exit 1
  fi
fi
EOF

  manifest_lines "${SERVICES[@]}" | while read -r svc kind rest; do
    case "$kind" in
      restart|start|enable|run)
        echo "if [ \"\$changed_$svc\" != 00 ]; then"
        case "$kind" in
          # Každý příkaz na vlastním řádku: v "a && b" by set -e selhání a přešel.
          restart) echo "  systemctl restart $(q "$rest")"; echo "  echo '  restart: $rest'" ;;
          start)   echo "  systemctl start --no-block $(q "$rest")"; echo "  echo '  start: $rest'" ;;
          # Při prvním zavedení služba bez vyplněných hesel nemusí naběhnout;
          # to není důvod zastavit zbytek nasazení.
          enable)  echo "  if systemctl enable --now $(q "$rest") 2>/dev/null; then echo '  enable: $rest'; else echo '  VAROVÁNÍ: $rest nenaběhl — doplň hesla, pak journalctl -u $rest' >&2; fi" ;;
          run)     echo "  ( $rest )"; echo "  echo '  run: $(printf '%s' "${rest%% *}" | tr -d "'")'" ;;
        esac
        echo "fi" ;;
    esac
  done
  echo 'rm -rf "$S"'
} > "$script"

if [ $DRY -eq 1 ]; then
  cat "$script"
  exit 0
fi

# --- nahrání ------------------------------------------------------------------------
step "Nahrání do ~/deploy-stage"
manifest_files "${SERVICES[@]}" | while read -r repo _; do
  mkdir -p "$stage/$(dirname "$repo")"
  if [ "$repo" = "infra/caddy/Caddyfile" ]; then
    sed "s/{{DOMAIN}}/$CLOCK_HOST/g" "$repo" > "$stage/$repo"
  else
    cp "$repo" "$stage/$repo"
  fi
done
# COPYFILE_DISABLE: jinak tar z macOS přibalí ._* soubory s metadaty.
COPYFILE_DISABLE=1 tar -C "$stage" -cf - infra \
  | "${SSH[@]}" "rm -rf '$STAGE' && mkdir '$STAGE' && tar xf - -C '$STAGE'"
echo "souborů: $(manifest_files "${SERVICES[@]}" | sort -u | wc -l | tr -d ' ')"

# changed_<svc> začíná jako "$FORCE$INIT" (00 = nic nevynucené) a put ho
# přepíše na 1; akce běží, když je cokoli jiného než 00.
step "Instalace"
"${SSH[@]}" "sudo bash -s" < "$script"

# --- ověření --------------------------------------------------------------------------
step "Otisky"
pairs="$(manifest_files "${SERVICES[@]}" | sort -u)"
remote_paths="$(printf '%s\n' "$pairs" | awk '{print $2}' | tr '\n' ' ')"
remote_sums="$("${SSH[@]}" "sudo md5sum $remote_paths")"
bad=0
while read -r repo server; do
  want="$(manifest_local_md5 "$repo" "$CLOCK_HOST")"
  have="$(printf '%s\n' "$remote_sums" | awk -v p="$server" '$2 == p {print $1}')"
  if [ "$want" != "$have" ]; then
    echo "  nesedí: $server"; bad=1
  fi
done <<< "$pairs"
[ $bad -eq 0 ] || die "nainstalované soubory nesedí s repem"
echo "  všechny sedí"

if [ $CHECK -eq 1 ]; then
  step "Kontrola"
  tools/check-stack.sh --deep
fi
