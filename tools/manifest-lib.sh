# Čtení infra/manifest.txt, sdílené tools/deploy.sh a tools/check-stack.sh.
# Načítá se přes `. tools/manifest-lib.sh`; potřebuje proměnnou REPO_ROOT.
# Psané pro bash 3.2 (výchozí na macOS): žádná asociativní pole ani mapfile.

MANIFEST="${MANIFEST:-$REPO_ROOT/infra/manifest.txt}"
DROPIN_SOURCE="infra/health/on-failure.conf"

# Řádky manifestu bez komentářů a prázdných řádků. S argumenty jen pro dané
# služby, v pořadí manifestu.
manifest_lines() {
  local wanted=" $* "
  grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | while read -r svc kind rest; do
    if [ $# -eq 0 ] || [ "${wanted#* $svc }" != "$wanted" ]; then
      printf '%s %s %s\n' "$svc" "$kind" "$rest"
    fi
  done
}

# Všechny služby v manifestu, každá jednou.
manifest_services() {
  grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | awk '!seen[$1]++ {print $1}'
}

# Dvojice "soubor-v-repu cesta-na-serveru" pro porovnání obsahu. Drop-iny
# se rozepíšou na jeden soubor pro každou hlídanou jednotku.
manifest_files() {
  manifest_lines "$@" | while read -r svc kind a b; do
    case "$kind" in
      file|exec|unit|sysunit|caddyfile) printf '%s %s\n' "$a" "$b" ;;
      dropin) printf '%s %s\n' "$DROPIN_SOURCE" "/etc/systemd/system/$a.d/on-failure.conf" ;;
    esac
  done
}

# MD5 lokálního souboru tak, jak má ležet na serveru (Caddyfile s dosazenou
# doménou). macOS má md5 -q, Linux md5sum.
manifest_local_md5() {
  local path="$1" host="$2" rendered
  if [ "$path" = "infra/caddy/Caddyfile" ]; then
    rendered="$(mktemp)"
    sed -e "s/{{DOMAIN}}/$host/g" -e "s/{{FLEET_DOMAIN}}/${FLEET_HOST:?Chybí FLEET_HOST v .env}/g" \
      "$REPO_ROOT/$path" > "$rendered"
    md5 -q "$rendered" 2>/dev/null || md5sum "$rendered" | cut -d' ' -f1
    rm -f "$rendered"
  else
    md5 -q "$REPO_ROOT/$path" 2>/dev/null || md5sum "$REPO_ROOT/$path" | cut -d' ' -f1
  fi
}
