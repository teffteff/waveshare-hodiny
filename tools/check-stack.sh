#!/usr/bin/env bash
# Zkontroluje, jestli serverová část hodin žije: kanál se zprávami, Home
# Assistant za HTTPS proxy a platnost certifikátu. Bez parametrů kontroluje
# jen zvenčí, takže nepotřebuje SSH klíč; --deep přidá kontroly na serveru.
#
#   tools/check-stack.sh          rychlá kontrola z internetu
#   tools/check-stack.sh --deep   + systemd jednotky a shoda infra/ se serverem
#
# Neshoda v --deep říká jen to, že se obsah liší; jestli je napřed repozitář
# (nenasazená změna) nebo server (změna bez commitu), pozná až člověk.
#
# Návratový kód 0 = vše v pořádku, 1 = aspoň jedna kontrola selhala.

set -uo pipefail

REPO_ROOT_EARLY="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Adresa stroje, uživatel a klíč nepatří do veřejného repozitáře, takže se
# berou z kořenového .env (stejného, ze kterého čte tools/generate_secrets.py)
# nebo z prostředí. Bez nich skript neví, na co se má ptát.
if [ -f "$REPO_ROOT_EARLY/.env" ]; then
  while IFS='=' read -r key value; do
    case "$key" in
      CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY)
        # eval kvůli $HOME v cestě ke klíči; hodnoty pocházejí z vlastního .env.
        [ -z "${!key:-}" ] && eval "$key=\"$value\""
        ;;
    esac
  done < <(grep -E '^(CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY)=' "$REPO_ROOT_EARLY/.env")
fi

HOST="${CLOCK_HOST:-}"
SSH_TARGET="${CLOCK_SSH:-}"
SSH_KEY="${CLOCK_SSH_KEY:-}"
if [ -z "$HOST" ]; then
  printf 'Chybí CLOCK_HOST. Doplň do .env v kořeni repozitáře:\n' >&2
  printf '  CLOCK_HOST=tvuj-stroj.example.net\n' >&2
  printf '  CLOCK_SSH=uzivatel@1.2.3.4\n' >&2
  printf '  CLOCK_SSH_KEY=$HOME/cesta/ke/klici.key\n' >&2
  exit 2
fi
FEED_URL="https://${HOST}/top.xml"
HA_URL="https://${HOST}/"
# Kanál se generuje 8x denně mezi 06:05 a 20:05, takže po noci je legitimně
# starý přes deset hodin. Práh je nad tím, ale pod celým dnem.
FEED_MAX_AGE_HOURS="${FEED_MAX_AGE_HOURS:-14}"
CERT_MIN_DAYS="${CERT_MIN_DAYS:-21}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Zachytit hned: níž se poziční parametry přepisují při rozebírání výstupů.
MODE="${1:-}"
failures=0

ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; failures=$((failures + 1)); }
warn() { printf '  \033[33mwarn\033[0m  %s\n' "$1"; }
head_() { printf '\n%s\n' "$1"; }

ssh_run() {
  ssh -i "$SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa \
      -o ConnectTimeout=15 -o BatchMode=yes "$SSH_TARGET" "$@" 2>/dev/null
}

head_ "Zvenčí (${HOST})"

# --- kanál se zprávami -------------------------------------------------------
feed_body="$(curl -fsS --max-time 20 "$FEED_URL" 2>/dev/null)"
if [ -z "$feed_body" ]; then
  bad "kanál $FEED_URL neodpovídá (curl selhal)"
else
  items="$(printf '%s' "$feed_body" | grep -c '<item>')"
  if [ "$items" -lt 1 ]; then
    bad "kanál odpovídá, ale nemá žádnou položku"
  else
    ok "kanál odpovídá, položek: $items"
  fi
  age_report="$(printf '%s' "$feed_body" | python3 -c '
import re, sys
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
body = sys.stdin.read()
m = re.search(r"<lastBuildDate>(.*?)</lastBuildDate>", body)
if not m:
    print("NOSTAMP 0"); raise SystemExit
try:
    built = parsedate_to_datetime(m.group(1))
except Exception:
    print("NOSTAMP 0"); raise SystemExit
if built.tzinfo is None:
    built = built.replace(tzinfo=timezone.utc)
hours = (datetime.now(timezone.utc) - built).total_seconds() / 3600
print(f"OK {hours:.1f}")
')"
  read -r age_status age_hours <<< "$age_report"
  if [ "${age_status:-NOSTAMP}" = "NOSTAMP" ]; then
    warn "kanál nemá čitelný lastBuildDate, stáří nelze ověřit"
  elif awk -v a="$age_hours" -v m="$FEED_MAX_AGE_HOURS" 'BEGIN{exit !(a > m)}'; then
    bad "kanál je starý $age_hours h (práh $FEED_MAX_AGE_HOURS h) — generátor nejspíš padá, viz news.service"
  else
    ok "kanál je čerstvý, stáří $age_hours h"
  fi
fi

# --- Home Assistant za proxy -------------------------------------------------
ha_code="$(curl -fsS -o /dev/null -w '%{http_code}' --max-time 20 "$HA_URL" 2>/dev/null)"
if [ "$ha_code" = "200" ]; then
  ok "Home Assistant přes HTTPS vrací 200"
elif [ "$ha_code" = "400" ]; then
  bad "Home Assistant vrací 400 — vrátila se past s X-Forwarded-For, viz infra/README.md"
else
  bad "Home Assistant přes HTTPS vrací '${ha_code:-nic}'"
fi

# --- certifikát --------------------------------------------------------------
cert="$(echo | openssl s_client -connect "${HOST}:443" -servername "$HOST" 2>/dev/null \
        | openssl x509 2>/dev/null)"
if [ -z "$cert" ]; then
  bad "certifikát se nepodařilo stáhnout (port 443 zavřený?)"
else
  not_after="$(printf '%s' "$cert" | openssl x509 -noout -enddate 2>/dev/null | cut -d= -f2)"
  if printf '%s' "$cert" | openssl x509 -checkend $((CERT_MIN_DAYS * 86400)) >/dev/null 2>&1; then
    ok "certifikát platí déle než $CERT_MIN_DAYS dní (do $not_after)"
  else
    bad "certifikát vyprší do $CERT_MIN_DAYS dní ($not_after) — obnova Caddy neprošla, zkontroluj porty 80/443 v OCI"
  fi
fi

# --- hloubková kontrola na serveru ------------------------------------------
if [ "$MODE" = "--deep" ]; then
  head_ "Na serveru (${SSH_TARGET})"
  if ! ssh_run true; then
    bad "SSH se nepřipojilo (klíč $SSH_KEY)"
  else
    for unit in news-web.service news.timer caddy.service; do
      state="$(ssh_run "systemctl is-active $unit")"
      if [ "$state" = "active" ]; then
        ok "$unit je active"
      else
        bad "$unit je '${state:-neznámý}'"
      fi
    done

    last_run="$(ssh_run "systemctl show news.service -p ExecMainStatus --value")"
    if [ "$last_run" = "0" ]; then
      ok "poslední běh news.service skončil úspěšně"
    else
      bad "poslední běh news.service skončil kódem '${last_run:-neznámý}' — journalctl -u news.service"
    fi

    # Obchvat s X-Forwarded-For: dokud HA na proxovanou hlavičku odpovídá 400,
    # nenačetl trusted_proxies a hlavičky v Caddyfile se odebírat nesmí. Testuje
    # se zevnitř, protože Caddy hlavičku zvenčí stejně utne.
    xff_code="$(ssh_run "curl -s -o /dev/null -w '%{http_code}' --max-time 10 -H 'X-Forwarded-For: 203.0.113.9' http://127.0.0.1:8123/")"
    if [ "$xff_code" = "200" ]; then
      ok "HA přijímá X-Forwarded-For — obchvat v Caddyfile už není potřeba, viz infra/README.md"
    elif [ "$xff_code" = "400" ]; then
      warn "HA na X-Forwarded-For vrací 400 — trusted_proxies není načtené, obchvat v Caddyfile musí zůstat"
    else
      warn "test X-Forwarded-For vrátil '${xff_code:-nic}'"
    fi

    head_ "Shoda infra/ se serverem"
    remote_sums="$(ssh_run 'md5sum /opt/news/generate.py /opt/news/serve.py /opt/news/news.service /opt/news/news.timer /opt/news/news-web.service; sudo md5sum /etc/caddy/Caddyfile /etc/systemd/system/caddy.service')"
    if [ -z "$remote_sums" ]; then
      warn "kontrolní součty ze serveru se nepodařilo přečíst"
    else
      # news.env.example schválně chybí: kopie v repu je opravená (Gemini),
      # zatímco na serveru zůstala původní s ANTHROPIC_API_KEY.
      while read -r sum path; do
        case "$path" in
          /opt/news/*)                    local_path="infra/news/$(basename "$path")" ;;
          /etc/caddy/Caddyfile)           local_path="infra/caddy/Caddyfile" ;;
          /etc/systemd/system/caddy.service) local_path="infra/caddy/caddy.service" ;;
          *) continue ;;
        esac
        if [ "$local_path" = "infra/caddy/Caddyfile" ]; then
          # Šablona: {{DOMAIN}} se dosadí, teprve pak má smysl porovnávat.
          rendered="$(mktemp)"
          sed "s/{{DOMAIN}}/$HOST/g" "$REPO_ROOT/$local_path" > "$rendered"
          local_sum="$(md5 -q "$rendered" 2>/dev/null || md5sum "$rendered" 2>/dev/null | cut -d' ' -f1)"
          rm -f "$rendered"
        else
          local_sum="$(md5 -q "$REPO_ROOT/$local_path" 2>/dev/null || md5sum "$REPO_ROOT/$local_path" 2>/dev/null | cut -d' ' -f1)"
        fi
        if [ "$sum" = "$local_sum" ]; then
          ok "$local_path odpovídá serveru"
        else
          bad "$local_path se liší od $path — nasaď repo na server, nebo commitni změnu ze serveru"
        fi
      done <<< "$remote_sums"
    fi
  fi
fi

head_ "Výsledek"
if [ "$failures" -eq 0 ]; then
  printf '  vše v pořádku\n\n'
  exit 0
fi
printf '  neúspěšných kontrol: %s\n\n' "$failures"
exit 1
