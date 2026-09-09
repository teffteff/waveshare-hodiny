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
      CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY|AGENDA_PASSWORD)
        # eval kvůli $HOME v cestě ke klíči; hodnoty pocházejí z vlastního .env.
        [ -z "${!key:-}" ] && eval "$key=\"$value\""
        ;;
    esac
  done < <(grep -E '^(CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY|AGENDA_PASSWORD)=' "$REPO_ROOT_EARLY/.env")
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
AGENDA_URL="https://${HOST}/agenda.json"
PLANES_URL="https://${HOST}/planes.json"
# Agenda je za heslem, kanál se zprávami ne. Jméno je natvrdo i v Caddyfile,
# tajemstvím je jen heslo, které leží v .env jako AGENDA_PASSWORD.
AGENDA_USER="${AGENDA_USER:-hodiny}"
HA_URL="https://${HOST}/"
# Kanál se generuje 8x denně mezi 06:05 a 20:05, takže po noci je legitimně
# starý přes deset hodin. Práh je nad tím, ale pod celým dnem.
FEED_MAX_AGE_HOURS="${FEED_MAX_AGE_HOURS:-14}"
# Agenda se obnovuje každých 15 minut, takže hodina už jsou čtyři zmeškané běhy
# za sebou. Práh je nad jedním výpadkem, ale hluboko pod celým dnem.
AGENDA_MAX_AGE_HOURS="${AGENDA_MAX_AGE_HOURS:-1}"
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

# --- agenda z kalendáře ------------------------------------------------------
# Agenda je od 9. 9. 2026 za heslem (basic_auth v Caddyfile), protože nese
# titulky událostí z rodinného kalendáře. Kontroluje se proto dvakrát: bez
# hesla musí přijít 401, teprve s heslem se kouká na obsah. První kontrola je
# tu proto, aby se odkryté agendy všiml skript, ne až cizí čtenář.
agenda_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "$AGENDA_URL")"
if [ "$agenda_public_code" = "401" ]; then
  ok "agenda je bez hesla nedostupná (401)"
else
  bad "agenda bez hesla vrací '${agenda_public_code:-nic}' místo 401 — v /etc/caddy/Caddyfile chybí basic_auth"
fi

agenda_body=""
if [ -z "${AGENDA_PASSWORD:-}" ]; then
  warn "v .env chybí AGENDA_PASSWORD, čerstvost agendy se nekontroluje"
else
  # Heslo se curlu předává souborem, ne přepínačem -u: příkazovou řádku
  # běžícího procesu si přečte kdokoli přes ps.
  agenda_curl_config="$(mktemp)"
  chmod 600 "$agenda_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$AGENDA_PASSWORD" > "$agenda_curl_config"
  agenda_body="$(curl -fsS --max-time 20 -K "$agenda_curl_config" "$AGENDA_URL" 2>/dev/null)"
  rm -f "$agenda_curl_config"
  if [ -z "$agenda_body" ]; then
    bad "agenda $AGENDA_URL neodpovídá, nebo neplatí heslo z .env (curl selhal)"
  fi
fi
if [ -n "$agenda_body" ]; then
  agenda_report="$(printf '%s' "$agenda_body" | python3 -c '
import json, sys
from datetime import datetime, timezone
try:
    data = json.loads(sys.stdin.read())
    built = datetime.fromisoformat(data["generated"])
    count = int(data.get("count", 0))
except Exception:
    print("BAD 0 0"); raise SystemExit
if built.tzinfo is None:
    built = built.replace(tzinfo=timezone.utc)
hours = (datetime.now(timezone.utc) - built).total_seconds() / 3600
print(f"OK {hours:.1f} {count}")
')"
  read -r agenda_status agenda_hours agenda_count <<< "$agenda_report"
  if [ "${agenda_status:-BAD}" = "BAD" ]; then
    bad "agenda odpovídá, ale není to platný JSON s polem generated"
  elif awk -v a="$agenda_hours" -v m="$AGENDA_MAX_AGE_HOURS" 'BEGIN{exit !(a > m)}'; then
    bad "agenda je stará $agenda_hours h (práh $AGENDA_MAX_AGE_HOURS h) — generátor nejspíš padá, viz agenda.service"
  else
    # Prázdná agenda je legitimní stav: kalendář prostě nic nemá. Proto warn,
    # ne FAIL — na rozdíl od kanálu, kde prázdno vždy znamená rozbitý běh.
    if [ "${agenda_count:-0}" -eq 0 ]; then
      warn "agenda je čerstvá ($agenda_hours h), ale nemá žádnou událost"
    else
      ok "agenda je čerstvá, stáří $agenda_hours h, událostí: $agenda_count"
    fi
  fi
fi

# --- letadla -----------------------------------------------------------------
# Přepravčí letadel je nepovinný: hodiny se bez něj ptají adsb.fi přímo. Když
# adresa neodpovídá vůbec, je to jen poznámka; když odpoví něčím, co není
# seznam letadel, je to chyba - obrazovka by zůstala prázdná.
#
# Dotaz jde na Brno a padesát námořních mil, tedy na to, na co se ptají hodiny.
# Prázdná obloha je legitimní odpověď (v noci nad menším městem), takže se
# nepočítá počet letadel, jen tvar odpovědi.
planes_body="$(curl -fsS --max-time 25 "${PLANES_URL}?lat=49.1951&lon=16.6068&dist=50.0" 2>/dev/null)"
if [ -z "$planes_body" ]; then
  warn "letadla na $PLANES_URL neodpovídají (nepovinná služba, hodiny umí i adsb.fi přímo)"
else
  planes_report="$(printf '%s' "$planes_body" | python3 -c '
import json, sys
try:
    data = json.loads(sys.stdin.read())
    aircraft = data["ac"]
except Exception:
    print("BAD 0"); raise SystemExit
if not isinstance(aircraft, list):
    print("BAD 0"); raise SystemExit
print(f"OK {len(aircraft)}")
')"
  read -r planes_status planes_count <<< "$planes_report"
  if [ "${planes_status:-BAD}" = "BAD" ]; then
    bad "letadla odpovídají, ale není to JSON s polem ac — firmware by nenačetl nic"
  else
    ok "letadla odpovídají, letadel v okruhu 50 NM: $planes_count, $(printf '%s' "$planes_body" | wc -c | tr -d ' ') B"
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
    for unit in news-web.service news.timer agenda-web.service agenda.timer planes-web.service caddy.service; do
      state="$(ssh_run "systemctl is-active $unit")"
      if [ "$state" = "active" ]; then
        ok "$unit je active"
      else
        bad "$unit je '${state:-neznámý}'"
      fi
    done

    for unit in news.service agenda.service; do
      last_run="$(ssh_run "systemctl show $unit -p ExecMainStatus --value")"
      if [ "$last_run" = "0" ]; then
        ok "poslední běh $unit skončil úspěšně"
      else
        bad "poslední běh $unit skončil kódem '${last_run:-neznámý}' — journalctl -u $unit"
      fi
    done

    # Reverzní proxy: HA na proxovaný požadavek vrací 400, dokud nemá zapnuté
    # use_x_forwarded_for a mezi trusted_proxies i 127.0.0.1 — forwarded.py
    # v obou případech rovnou raise HTTPBadRequest. 200 tedy znamená, že běžící
    # proces nastavení má. Obchvat v Caddyfile je od 8. 9. 2026 pryč, takže 400
    # už není očekávaný stav, ale regrese. Testuje se zevnitř, protože Caddy
    # hlavičku zvenčí stejně utne.
    xff_code="$(ssh_run "curl -s -o /dev/null -w '%{http_code}' --max-time 10 -H 'X-Forwarded-For: 203.0.113.9' http://127.0.0.1:8123/")"
    if [ "$xff_code" = "200" ]; then
      ok "HA přijímá X-Forwarded-For — zná skutečnou IP klienta"
    elif [ "$xff_code" = "400" ]; then
      bad "HA na X-Forwarded-For vrací 400 — use_x_forwarded_for nebo trusted_proxies chybí, viz infra/README.md"
    else
      warn "test X-Forwarded-For vrátil '${xff_code:-nic}'"
    fi

    head_ "Shoda infra/ se serverem"
    remote_sums="$(ssh_run 'md5sum /opt/news/generate.py /opt/news/serve.py /opt/news/news.service /opt/news/news.timer /opt/news/news-web.service /opt/agenda/generate.py /opt/agenda/serve.py /opt/agenda/agenda.service /opt/agenda/agenda.timer /opt/agenda/agenda-web.service /opt/planes/serve.py /opt/planes/planes-web.service; sudo md5sum /etc/caddy/Caddyfile /etc/systemd/system/caddy.service')"
    if [ -z "$remote_sums" ]; then
      warn "kontrolní součty ze serveru se nepodařilo přečíst"
    else
      # news.env.example schválně chybí: kopie v repu je opravená (Gemini),
      # zatímco na serveru zůstala původní s ANTHROPIC_API_KEY.
      while read -r sum path; do
        case "$path" in
          /opt/news/*)                    local_path="infra/news/$(basename "$path")" ;;
          /opt/agenda/*)                  local_path="infra/agenda/$(basename "$path")" ;;
          /opt/planes/*)                  local_path="infra/planes/$(basename "$path")" ;;
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
