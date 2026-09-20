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
      CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY|AGENDA_PASSWORD|SETTINGS_PASSWORD|PLANES_PASSWORD|LIGHTNING_PASSWORD|SCHOOL_PASSWORD|SATELLITES_PASSWORD)
        # eval kvůli $HOME v cestě ke klíči; hodnoty pocházejí z vlastního .env.
        [ -z "${!key:-}" ] && eval "$key=\"$value\""
        ;;
    esac
  done < <(grep -E '^(CLOCK_HOST|CLOCK_SSH|CLOCK_SSH_KEY|AGENDA_PASSWORD|SETTINGS_PASSWORD|PLANES_PASSWORD|LIGHTNING_PASSWORD|SCHOOL_PASSWORD|SATELLITES_PASSWORD)=' "$REPO_ROOT_EARLY/.env")
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
LIGHTNING_URL="https://${HOST}/lightning.json"
SETTINGS_URL="https://${HOST}/settings/"
SCHOOL_URL="https://${HOST}/school.json"
SATELLITES_URL="https://${HOST}/satellites.json"
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

# --- zálohy nastavení --------------------------------------------------------
# Zálohy nesou token Home Assistantu a heslo webu, takže stejně jako u agendy
# musí bez hesla přijít 401. S heslem z .env (SETTINGS_PASSWORD) se ověří, že
# server odpovídá seznamem. Prázdný seznam je v pořádku - nikdo nic nezálohoval.
settings_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "$SETTINGS_URL")"
if [ "$settings_public_code" = "401" ]; then
  ok "zálohy nastavení jsou bez hesla nedostupné (401)"
elif [ "$settings_public_code" = "200" ]; then
  bad "zálohy nastavení bez hesla vrací 200 — v /etc/caddy/Caddyfile chybí basic_auth, tokeny jsou venku"
else
  warn "zálohy nastavení bez hesla vrací '${settings_public_code:-nic}' (služba nasazená? viz infra/README.md)"
fi
if [ -n "${SETTINGS_PASSWORD:-}" ]; then
  settings_curl_config="$(mktemp)"
  chmod 600 "$settings_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$SETTINGS_PASSWORD" > "$settings_curl_config"
  settings_body="$(curl -fsS --max-time 20 -K "$settings_curl_config" "$SETTINGS_URL" 2>/dev/null)"
  rm -f "$settings_curl_config"
  settings_count="$(printf '%s' "$settings_body" | python3 -c '
import json, sys
try:
    print(len(json.loads(sys.stdin.read())["backups"]))
except Exception:
    print("BAD")
')"
  if [ "$settings_count" = "BAD" ]; then
    bad "zálohy nastavení s heslem z .env neodpovídají seznamem (heslo, nebo settings-web.service)"
  else
    ok "zálohy nastavení odpovídají, uložených záloh: $settings_count"
  fi
fi

# --- letadla -----------------------------------------------------------------
# Přepravčí letadel je nepovinný: hodiny se bez něj ptají adsb.fi přímo. Když
# adresa neodpovídá vůbec, je to jen poznámka; když odpoví něčím, co není
# seznam letadel, je to chyba - obrazovka by zůstala prázdná.
#
# Dotaz jde na Ondřejov a padesát námořních mil, tedy na to, na co se ptají hodiny.
# Prázdná obloha je legitimní odpověď (v noci nad menším městem), takže se
# nepočítá počet letadel, jen tvar odpovědi.
#
# Od 13. 9. 2026 jsou letadla za heslem (PLANES_HASH). Data jsou veřejná, heslo
# brání tomu, aby si cizí přes server čerpal limit adsb.fi. Bez hesla proto
# musí přijít 401, stejně jako u agendy.
PLANES_QUERY="lat=49.90461&lon=14.7842&dist=50.0"
planes_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 25 "${PLANES_URL}?${PLANES_QUERY}")"
if [ "$planes_public_code" = "401" ]; then
  ok "letadla jsou bez hesla nedostupná (401)"
elif [ "$planes_public_code" = "200" ]; then
  bad "letadla bez hesla vrací 200 — v /etc/caddy/Caddyfile chybí basic_auth, adsb.fi se dá čerpat přes nás"
else
  warn "letadla bez hesla vrací '${planes_public_code:-nic}' (nepovinná služba, hodiny umí i adsb.fi přímo)"
fi

planes_body=""
if [ -z "${PLANES_PASSWORD:-}" ]; then
  warn "v .env chybí PLANES_PASSWORD, obsah letadel se nekontroluje"
else
  planes_curl_config="$(mktemp)"
  chmod 600 "$planes_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$PLANES_PASSWORD" > "$planes_curl_config"
  planes_body="$(curl -fsS --max-time 25 -K "$planes_curl_config" "${PLANES_URL}?${PLANES_QUERY}" 2>/dev/null)"
  rm -f "$planes_curl_config"
  if [ -z "$planes_body" ]; then
    warn "letadla na $PLANES_URL s heslem z .env neodpovídají (heslo, nebo planes-web.service)"
  fi
fi
if [ -n "$planes_body" ]; then
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

# --- blesky ------------------------------------------------------------------
# Data sítě Blitzortung se nesmí dál zveřejňovat, takže bez hesla musí přijít 401.
# S heslem se kontroluje tvar odpovědi; prázdný seznam úderů je legitimní (když
# neblýská), "live": false jen chvíli po probuzení spojení na LightningMaps.
LIGHTNING_QUERY="lat=49.90461&lon=14.7842&r=150"
lightning_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 25 "${LIGHTNING_URL}?${LIGHTNING_QUERY}")"
if [ "$lightning_public_code" = "401" ]; then
  ok "blesky jsou bez hesla nedostupné (401)"
elif [ "$lightning_public_code" = "200" ]; then
  bad "blesky bez hesla vrací 200 — v /etc/caddy/Caddyfile chybí basic_auth, data Blitzortung jsou veřejně"
else
  warn "blesky bez hesla vrací '${lightning_public_code:-nic}' (nepovinná služba)"
fi

lightning_body=""
if [ -z "${LIGHTNING_PASSWORD:-}" ]; then
  warn "v .env chybí LIGHTNING_PASSWORD, obsah blesků se nekontroluje"
else
  lightning_curl_config="$(mktemp)"
  chmod 600 "$lightning_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$LIGHTNING_PASSWORD" > "$lightning_curl_config"
  lightning_body="$(curl -fsS --max-time 25 -K "$lightning_curl_config" "${LIGHTNING_URL}?${LIGHTNING_QUERY}" 2>/dev/null)"
  rm -f "$lightning_curl_config"
  if [ -z "$lightning_body" ]; then
    warn "blesky na $LIGHTNING_URL s heslem z .env neodpovídají (heslo, nebo lightning-web.service)"
  fi
fi
if [ -n "$lightning_body" ]; then
  lightning_report="$(printf '%s' "$lightning_body" | python3 -c '
import json, sys
try:
    data = json.loads(sys.stdin.read())
    strokes = data["strokes"]
    live = bool(data.get("live"))
except Exception:
    print("BAD 0 0"); raise SystemExit
if not isinstance(strokes, list):
    print("BAD 0 0"); raise SystemExit
print(f"OK {len(strokes)} {int(live)}")
')"
  read -r lightning_status lightning_count lightning_live <<< "$lightning_report"
  if [ "${lightning_status:-BAD}" = "BAD" ]; then
    bad "blesky odpovídají, ale není to JSON s polem strokes — firmware by nenačetl nic"
  elif [ "$lightning_live" = "1" ]; then
    ok "blesky odpovídají živě, úderů v okruhu 150 km: $lightning_count"
  else
    warn "blesky odpovídají, ale spojení na LightningMaps se teprve otevírá (zkus znovu za pár sekund)"
  fi
fi

# --- družice -------------------------------------------------------------------
# Dráhy jsou veřejná data, heslo chrání procesor serveru a polohu v dotazu, takže
# bez hesla musí přijít 401. S heslem se kontroluje tvar odpovědi a stáří drah:
# server je obnovuje po šesti hodinách, po dni už něco se stahováním z CelesTraku
# není v pořádku. 503 chvíli po startu znamená, že se dráhy teprve stahují.
SATELLITES_QUERY="lat=49.90&lon=14.78&groups=stations,visual,weather&minel=0"
satellites_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 25 "${SATELLITES_URL}?${SATELLITES_QUERY}")"
if [ "$satellites_public_code" = "401" ]; then
  ok "družice jsou bez hesla nedostupné (401)"
elif [ "$satellites_public_code" = "200" ]; then
  bad "družice bez hesla vrací 200 — v /etc/caddy/Caddyfile chybí basic_auth, výpočet drah je komukoli k dispozici"
else
  warn "družice bez hesla vrací '${satellites_public_code:-nic}' (nepovinná služba)"
fi

satellites_body=""
satellites_code=""
if [ -z "${SATELLITES_PASSWORD:-}" ]; then
  warn "v .env chybí SATELLITES_PASSWORD, obsah družic se nekontroluje"
else
  satellites_curl_config="$(mktemp)"
  chmod 600 "$satellites_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$SATELLITES_PASSWORD" > "$satellites_curl_config"
  satellites_body="$(curl -sS --max-time 25 -K "$satellites_curl_config" -w '\n%{http_code}' "${SATELLITES_URL}?${SATELLITES_QUERY}" 2>/dev/null)"
  rm -f "$satellites_curl_config"
  satellites_code="$(printf '%s' "$satellites_body" | tail -n 1)"
  satellites_body="$(printf '%s' "$satellites_body" | sed '$d')"
  if [ "$satellites_code" = "503" ]; then
    warn "družice odpovídají 503 — server teprve stahuje dráhy z CelesTraku, nebo jsou starší než týden (journalctl -u satellites-web)"
    satellites_body=""
  elif [ "$satellites_code" != "200" ]; then
    warn "družice na $SATELLITES_URL s heslem z .env vrací '${satellites_code:-nic}' (heslo, nebo satellites-web.service)"
    satellites_body=""
  fi
fi
if [ -n "$satellites_body" ]; then
  satellites_report="$(printf '%s' "$satellites_body" | python3 -c '
import json, sys, time
try:
    data = json.loads(sys.stdin.read())
    sats = data["sats"]
    age = int(data["age"])
    skew = abs(time.time() - int(data["time"]))
except Exception:
    print("BAD 0 0 0 0"); raise SystemExit
if data.get("v") != 1 or not isinstance(sats, list):
    print("BAD 0 0 0 0"); raise SystemExit
problem = 1 if data.get("problem") or data.get("pending") else 0
print(f"OK {len(sats)} {age} {int(skew)} {problem}")
')"
  read -r satellites_status satellites_count satellites_age satellites_skew satellites_problem <<< "$satellites_report"
  if [ "${satellites_status:-BAD}" = "BAD" ]; then
    bad "družice odpovídají, ale není to JSON verze 1 se sats — firmware by nenačetl nic"
  elif [ "$satellites_skew" -gt 60 ]; then
    bad "čas odpovědi družic se liší o $satellites_skew s — server nemá správné hodiny, dráhy na displeji by ležely jinde"
  elif [ "$satellites_age" -gt 24 ]; then
    bad "dráhy družic jsou staré $satellites_age h — stahování z CelesTraku neprochází (curl 127.0.0.1:8095/satellites/status na serveru)"
  elif [ "$satellites_problem" = "1" ]; then
    warn "družice odpovídají, ale některá skupina chybí nebo je stará (curl 127.0.0.1:8095/satellites/status na serveru)"
  else
    ok "družice odpovídají, nad obzorem: $satellites_count, stáří drah $satellites_age h"
  fi
fi

# --- rozvrh a úkoly ---------------------------------------------------------
# Škola nese jméno dítěte a jeho úkoly, takže bez hesla musí přijít 401. S heslem
# z .env (SCHOOL_PASSWORD) se kontroluje čerstvost. Ve dne je práh 4 h (nejdelší
# odstup jsou tři hodiny), o prázdninách 7 h (po šesti), v tichu 22–6 h 10 h
# a o prázdninové noci 14 h, protože se mezi 22. a 5. hodinou nestahuje vůbec.
# SCHOOL_MAX_AGE_HOURS mění jen denní práh. Neprázdné "problem"
# říká, že poslední stažení ze Školy OnLine selhalo a hodiny ukazují starší data.
# Prázdný rozvrh je legitimní (prázdniny), proto se nepočítá.
SCHOOL_MAX_AGE_HOURS="${SCHOOL_MAX_AGE_HOURS:-4}"
school_public_code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "$SCHOOL_URL")"
if [ "$school_public_code" = "401" ]; then
  ok "rozvrh je bez hesla nedostupný (401)"
elif [ "$school_public_code" = "200" ]; then
  bad "rozvrh bez hesla vrací 200 — v /etc/caddy/Caddyfile chybí basic_auth, úkoly dítěte jsou venku"
else
  warn "rozvrh bez hesla vrací '${school_public_code:-nic}' (služba nasazená? viz infra/README.md)"
fi

school_body=""
if [ -z "${SCHOOL_PASSWORD:-}" ]; then
  warn "v .env chybí SCHOOL_PASSWORD, obsah rozvrhu se nekontroluje"
else
  school_curl_config="$(mktemp)"
  chmod 600 "$school_curl_config"
  printf 'user = "%s:%s"\n' "$AGENDA_USER" "$SCHOOL_PASSWORD" > "$school_curl_config"
  school_body="$(curl -fsS --max-time 20 -K "$school_curl_config" "$SCHOOL_URL" 2>/dev/null)"
  rm -f "$school_curl_config"
  if [ -z "$school_body" ]; then
    bad "rozvrh $SCHOOL_URL neodpovídá, nebo neplatí heslo z .env (503 = první stažení ze Školy OnLine neprošlo nebo jsou data starší než 14 h, viz journalctl -u school-web)"
  fi
fi
if [ -n "$school_body" ]; then
  school_report="$(printf '%s' "$school_body" | SCHOOL_DAY_LIMIT="$SCHOOL_MAX_AGE_HOURS" python3 -c '
import json, os, sys
from datetime import datetime, timezone
from zoneinfo import ZoneInfo
try:
    data = json.loads(sys.stdin.read())
    generated = datetime.fromisoformat(data["generated"])
    lessons = sum(len(day["lessons"]) for day in data["days"])
    # Server s vypnutymi ukoly klic "homework" neposila (SCHOOL_HOMEWORK=0).
    homework = len(data["homework"]) if "homework" in data else "vypnuto"
    meals = len(data["meals"]) if "meals" in data else "vypnuto"
except Exception:
    print("BAD 0 0 0 0 0 0"); raise SystemExit
hours = (datetime.now(timezone.utc) - generated).total_seconds() / 3600
problem = 1 if data.get("problem") else 0
local = datetime.now(ZoneInfo("Europe/Prague"))
quiet = local.hour >= 22 or local.hour < 6
holidays = not data["days"]
limit = float(os.environ["SCHOOL_DAY_LIMIT"])
if holidays:
    limit = max(limit, 14.0 if quiet else 7.0)
elif quiet:
    limit = max(limit, 10.0)
print(f"OK {hours:.1f} {lessons} {homework} {meals} {problem} {limit:g}")
')"
  read -r school_status school_hours school_lessons school_homework school_meals school_problem school_limit <<< "$school_report"
  if [ "${school_status:-BAD}" = "BAD" ]; then
    bad "rozvrh odpovídá, ale není to JSON s generated a days"
  elif awk -v a="$school_hours" -v m="$school_limit" 'BEGIN{exit !(a > m)}'; then
    bad "rozvrh je starý $school_hours h (práh $school_limit h) — stahování ze Školy OnLine padá, viz journalctl -u school-web"
  elif [ "$school_problem" = "1" ]; then
    warn "rozvrh je z doby před $school_hours h, poslední stažení selhalo — journalctl -u school-web"
  else
    ok "rozvrh je čerstvý, stáří $school_hours h, hodin: $school_lessons, úkolů: $school_homework, obědů: $school_meals"
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
    for unit in news-web.service news.timer agenda-web.service agenda.timer planes-web.service lightning-web.service settings-web.service school-web.service satellites-web.service rain-web.service caddy.service backup.timer; do
      state="$(ssh_run "systemctl is-active $unit")"
      if [ "$state" = "active" ]; then
        ok "$unit je active"
      else
        bad "$unit je '${state:-neznámý}'"
      fi
    done

    for unit in news.service agenda.service backup.service; do
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
    # Přes sudo: /opt/agenda, /opt/settings a /opt/school jsou jen pro své služby (750/700),
    # opc do nich bez sudo nevidí.
    remote_sums="$(ssh_run 'sudo md5sum /opt/news/generate.py /opt/news/serve.py /opt/news/locations.py /opt/news/news.service /opt/news/news.timer /opt/news/news-web.service /opt/agenda/generate.py /opt/agenda/feed.py /opt/agenda/serve.py /opt/agenda/agenda.service /opt/agenda/agenda.timer /opt/agenda/agenda-web.service /opt/planes/serve.py /opt/planes/planes-web.service /opt/lightning/serve.py /opt/lightning/lightning-web.service /opt/settings/serve.py /opt/settings/settings-web.service /opt/school/feed.py /opt/school/serve.py /opt/school/school-web.service /opt/satellites/serve.py /opt/satellites/satellites-web.service /opt/satellites/requirements.txt /opt/rain/serve.py /opt/rain/rain-web.service /etc/caddy/Caddyfile /etc/systemd/system/caddy.service /opt/backup/backup.sh /etc/systemd/system/backup.service /etc/systemd/system/backup.timer')"
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
          /opt/lightning/*)               local_path="infra/lightning/$(basename "$path")" ;;
          /opt/settings/*)                local_path="infra/settings/$(basename "$path")" ;;
          /opt/school/*)                  local_path="infra/school/$(basename "$path")" ;;
          /opt/satellites/*)              local_path="infra/satellites/$(basename "$path")" ;;
          /opt/rain/*)                    local_path="infra/rain/$(basename "$path")" ;;
          /etc/caddy/Caddyfile)           local_path="infra/caddy/Caddyfile" ;;
          /etc/systemd/system/caddy.service) local_path="infra/caddy/caddy.service" ;;
          /opt/backup/*)                  local_path="infra/backup/$(basename "$path")" ;;
          /etc/systemd/system/backup.*)   local_path="infra/backup/$(basename "$path")" ;;
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
