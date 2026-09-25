#!/bin/sh
# Zaloha toho, co na serveru neni v zadnem repozitari: konfigurace Home
# Assistanta, stavove databaze hlidacu a statistik, maly stav sluzeb, fotky
# hlidacu a soubory s hesly. Kod sluzeb se zalohovat nemusi, ten je v infra/
# a v repozitarich sluzeb; obnova stroje je popsana v infra/README.md, oddil
# "Obnova serveru".
#
# Archiv na stroji je jen mezistupen. Smysl dostane az tim, ze si ho nekdo
# stahne pryc - tools/pull-backup.sh. Zaloha na stejnem disku neochrani pred
# nicim krome vlastniho rm. Druha kopie jde zasifrovana do OCI Object Storage
# (BACKUP_OFFSITE_URL), aby ztrata Macu i stroje naraz neznamenala ztratu dat.
#
# Co roste bez konce, do nocniho archivu nepatri - kazdy by byl vetsi nez
# predchozi. Uzavrene mesice velkych tabulek (BACKUP_MONTHLY) a fotky
# (BACKUP_PHOTOS) jdou jednou do trvaleho archivu (BACKUP_ARCHIVE_URL) a nocni
# zaloha je vynecha teprve potom, co nahrani proslo.
#
#   backup.sh           nocni zaloha
#   backup.sh --audit   jen vypise data na serveru, ktera zadna zaloha nekryje
#
# Nenulovy konec = neco se nezalohovalo (selhany snimek, nahrani, nekryta
# data). Jednotka pak selze a OnFailure= posle push.
set -eu

: "${BACKUP_DEST:=/opt/backup/data}"
# Jak dlouho archivy na stroji zustavaji, ve dnech.
: "${BACKUP_KEEP_DAYS:=7}"
# Kolik nejnovejsich prezije vzdycky, i kdyz uz jsou starsi (viz prorezani niz).
: "${BACKUP_KEEP_MIN:=3}"
# Adresar, ktery je do kontejneru HA namountovany jako /config. Prazdne =
# preskocit. Skutecnou cestu rekne:
#   docker inspect homeassistant --format '{{json .HostConfig.Binds}}'
: "${BACKUP_HA_CONFIG:=}"
# Historie senzoru (home-assistant_v2.db, radove desitky MB). 0 = jen
# konfigurace. Sablony a automatizace na ni nestoji, grafy ano.
: "${BACKUP_HA_HISTORY:=1}"
# Soubory s hesly a klici. Chybejici se preskoci - stroj nemusi mit vsechno.
# Vyslovne prazdna hodnota znamena "zadne"; proto "=" a ne ":=" niz.
: "${BACKUP_SECRETS=/etc/caddy/caddy.env /opt/news/news.env /opt/agenda/agenda.env /opt/agenda/key.json /opt/school/school.env /opt/alerts/alerts.env /opt/health/health.env /opt/backup/backup.env /opt/watch/watch.env /opt/ou-watch/watch.env /opt/fleet/fleet.env /opt/llm/llm.env /opt/radar/radar.env /opt/hodiny-stats/stats.env /opt/hodiny-stats/ntfy.env}"
# Databaze, ktere se za behu zapisuji. Kopiruji se pres sqlite3 .backup, ne
# cp: prosty cp za behu utrhne stranku uprostred transakce a vysledek je
# nepouzitelny. Hlidaci si v nich drzi, co uz videli.
# radar.sqlite roste o ~13 MB denne; jeho velke tabulky viz BACKUP_MONTHLY.
# stats.sqlite (repozitar hodiny-stats) roste o necely megabajt mesicne.
: "${BACKUP_SQLITE=/var/lib/watch/state.db /var/lib/ou-watch/state.db /var/lib/radar/radar.sqlite /var/lib/hodiny-stats/stats.sqlite /opt/llm/state/usage.sqlite}"
# Maly stav sluzeb (JSON, desitky kB), kopiruje se, jak lezi: registrovane
# hodiny a TOTP, nastaveni hodin, stav upozorneni a hlaseni poruch, snimek
# Skoly OnLine. Databaze sem nepatri, ty jdou pres BACKUP_SQLITE.
: "${BACKUP_SERVICE_STATE=/opt/alerts/state /opt/fleet/state /opt/settings/data /var/lib/school /var/lib/health}"
# Cokoli navic, oddelene mezerami. Svety Minecraftu sem patri jen tehdy, kdyz
# je na ne dost mista - jsou radove GB a meni se porad.
: "${BACKUP_EXTRA:=}"
# Tabulky, ktere rostou bez konce, jako databaze:tabulka:sloupec (cas
# v sekundach od 1970, UTC). Kazdy uzavreny mesic odejde jednou jako vlastni
# archiv do BACKUP_ARCHIVE_URL (a do $BACKUP_DEST/monthly, odkud si ho stahne
# Mac) a z nocniho snimku databaze se pak vypusti. Databaze musi byt
# i v BACKUP_SQLITE. Bez BACKUP_ARCHIVE_URL se nevypousti nic.
: "${BACKUP_MONTHLY=/var/lib/radar/radar.sqlite:points:ts /var/lib/radar/radar.sqlite:upper_air:ts}"
# Adresare fotek s nemennymi soubory (jmeno = otisk obsahu). Kazda fotka
# odejde do BACKUP_ARCHIVE_URL jednou; co uz odeslo, pamatuje $BACKUP_LEDGER.
# Hlidac fotky po case maze, archiv si je necha.
: "${BACKUP_PHOTOS=/var/lib/watch/cache/images /var/lib/ou-watch/cache/images}"
# Strop na jeden beh, aby prvni nahrani vsech fotek (~2400 kusu, kazdy zvlastnim
# curl) nepreteklo TimeoutStartSec; zbytek odejde dalsi noc. Bezne pribyva
# kolem 150 fotek denne.
: "${BACKUP_PHOTOS_PER_RUN:=1500}"
# Co audit (--audit) nema hlasit: data, ktera se obnovi sama, a adresare
# v /opt, ktere nejsou nase sluzby.
: "${BACKUP_IGNORE=/opt/containerd /opt/rh /opt/unified-monitoring-agent /opt/backup/data /opt/news/state /var/lib/bus /var/lib/radar/collector-status.json /var/lib/hodiny-stats/collector-status.json}"
# Kopie mimo stroj. Adresa je pre-authenticated request (PAR) na bucket
# s pravem jen zapisovat objekty, koncici na /o/; prazdna = kopie se nedela.
# Nese tajny token, proto jen v backup.env (600) a nikdy v logu.
: "${BACKUP_OFFSITE_URL:=}"
# Trvaly archiv: totez, ale bucket BEZ pravidla, ktere maze stare objekty.
# Mesice velkych tabulek a fotky tam jsou jedinou kopii mimo stroj, takze je
# 30denni pravidlo bucketu s nocnimi kopiemi nesmi smazat.
: "${BACKUP_ARCHIVE_URL:=}"
# Verejny klic, kterym se kopie sifruje (infra/backup/offsite-key.asc). Soukromy
# ke nemu na serveru neni: kdo stroj ovladne, kopie nerozsifruje.
: "${BACKUP_OFFSITE_KEY:=/opt/backup/offsite-key.asc}"
# Evidence toho, co uz je v trvalem archivu (PAR neumi objekty vypsat).
# Ztrata neublizi: vse by jen odeslo znovu.
: "${BACKUP_LEDGER:=/opt/backup/ledger}"

log() { echo "backup: $*"; }
rc=0
# Varovani, kvuli kteremu jednotka skonci chybou - neco zustalo bez zalohy.
fail() { log "VAROVANI: $*"; rc=1; }

host="$(hostname -s)"
# Fotky se kryji, jen kdyz maji kam odejit.
photos_covered=""
[ -n "$BACKUP_ARCHIVE_URL" ] && photos_covered="$BACKUP_PHOTOS"

in_list() { # cesta seznam... - je cesta v seznamu, nebo pod nekterou z polozek?
  p="$1"; shift
  for e in "$@"; do
    case "$p" in "$e"|"$e"/*) return 0 ;; esac
  done
  return 1
}

# Soubory s daty sluzeb, ktere nic nekryje. Hleda se tam, kam je sluzby
# ukladaji: hesla a databaze v /opt/<sluzba>/, cely /opt/<sluzba>/state,
# /opt/<sluzba>/data a /var/lib/<sluzba>. Vic nez tri soubory v jednom
# adresari se hlasi jako adresar.
audit() {
  # shellcheck disable=SC2086
  for opt in /opt/*/; do
    opt="${opt%/}"
    name="$(basename "$opt")"
    in_list "$opt" $BACKUP_IGNORE && continue
    find "$opt" -maxdepth 1 -type f \( -name '*.env' -o -name 'key.json' -o -name '*.sqlite' -o -name '*.db' \) 2>/dev/null
    for sub in "$opt/state" "$opt/data" "/var/lib/$name"; do
      [ -d "$sub" ] && find "$sub" -type f 2>/dev/null
    done
  done | while read -r f; do
    case "$f" in *-wal|*-shm|*-journal) continue ;; esac
    in_list "$f" $BACKUP_IGNORE && continue
    case "$f" in
      *.sqlite|*.db)
        in_list "$f" $BACKUP_SQLITE && continue
        # cp za behu databazi rozbije, i kdyz ji kryje jiny seznam
        echo "$f [sqlite mimo BACKUP_SQLITE]"
        continue
        ;;
    esac
    in_list "$f" $BACKUP_SECRETS $BACKUP_SERVICE_STATE $BACKUP_EXTRA $photos_covered && continue
    echo "$f"
  done | awk '
    / \[/ { print; next }
    { d = $0; sub(/\/[^\/]*$/, "", d); n[d]++; f[d] = f[d] "\n" $0 }
    END { for (d in n) if (n[d] > 3) print d "/ (" n[d] " souboru)"; else printf "%s", substr(f[d], 2) "\n" }' \
  | sort
}

if [ "${1:-}" = "--audit" ]; then
  out="$(audit)"
  [ -z "$out" ] && { echo "Vsechna nalezena data jsou zalohovana."; exit 0; }
  echo "Bez zalohy:"; echo "$out" | sed 's/^/  /'
  [ -z "$BACKUP_ARCHIVE_URL" ] && echo "(fotky se kryji az s BACKUP_ARCHIVE_URL)"
  exit 1
fi

stamp="$(date -u +%Y%m%d-%H%M%S)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT INT TERM

files="$stage/files"
manifest="$stage/MANIFEST"
mkdir -p "$files"

{
  echo "Zaloha serveru hodin"
  echo "vytvoreno: $(date -u +%Y-%m-%dT%H:%M:%SZ) UTC"
  echo "stroj:     $host"
  echo
  echo "Obnova: rozbal a vrat soubory z files/ na stejne cesty, prava"
  echo "podle infra/README.md (hesla 600, key.json 600). Kod sluzeb sem"
  echo "nepatri, ten se nasazuje z repozitare. Mesice velkych tabulek a fotky"
  echo "jsou v trvalem archivu, viz infra/README.md, oddil Zalohy dat."
  echo
  echo "Obsah:"
} > "$manifest"

# Jeden soubor nebo adresar tak, jak lezi.
stage_path() {
  src="$1"
  if [ ! -e "$src" ]; then
    log "preskoceno (neexistuje): $src"
    echo "  - $src [chybi]" >> "$manifest"
    return 0
  fi
  mkdir -p "$files$(dirname "$src")"
  cp -a "$src" "$files$(dirname "$src")/"
  echo "  - $src" >> "$manifest"
}

# Konzistentni snimek zive sqlite databaze.
stage_sqlite() {
  src="$1"
  if [ ! -e "$src" ]; then
    log "preskoceno (neexistuje): $src"
    echo "  - $src [chybi]" >> "$manifest"
    return 0
  fi
  mkdir -p "$files$(dirname "$src")"
  out="$files$src"
  # .timeout necha .backup pockat, kdyz zrovna bezi timer hlidace, misto aby
  # spadl na "database is locked".
  if printf '.timeout 30000\n.backup %s\n' "'$out'" | sqlite3 "$src" 2>"$stage/sqlite.err"; then
    check="$(sqlite3 "$out" 'PRAGMA integrity_check;' 2>/dev/null || echo failed)"
    if [ "$check" = "ok" ]; then
      echo "  - $src [sqlite, integrity_check ok]" >> "$manifest"
    else
      fail "$src se zkopiroval, ale integrity_check rekl '$check'"
      echo "  - $src [sqlite, integrity_check: $check]" >> "$manifest"
    fi
  else
    fail "sqlite zalohu $src se nepodarilo porizdit: $(cat "$stage/sqlite.err")"
    echo "  - $src [SELHALO]" >> "$manifest"
    rm -f "$out"
  fi
}

# Sifruje se verejnym klicem v docasne klicence, ne v klicence roota: nic se
# nikam neimportuje natrvalo a zmena klice je jen novy soubor.
keyring=""
fingerprint=""
encrypt() { # vstup vystup
  if [ -z "$keyring" ]; then
    keyring="$stage/gnupg"
    mkdir -m 700 "$keyring"
    gpg2 -q --homedir "$keyring" --batch --import "$BACKUP_OFFSITE_KEY" \
      || { rm -rf "$keyring"; keyring=""; return 1; }
    fingerprint="$(gpg2 --homedir "$keyring" --with-colons --list-keys | awk -F: '/^fpr/ {print $10; exit}')"
  fi
  gpg2 -q --homedir "$keyring" --batch --trust-model always \
    --encrypt --recipient "$fingerprint" --output "$2" "$1"
}

# -f: HTTP chyba = nenulovy kod. curl pri chybe vypise jen kod, ne adresu,
# takze token z PAR se do zurnalu nedostane.
upload() { # soubor zakladni-URL objekt
  curl -fsS --retry 3 --retry-delay 30 --max-time 900 -o /dev/null \
    -T "$1" "${2%/}/$3"
}

# shellcheck disable=SC2086
for f in $BACKUP_SECRETS; do stage_path "$f"; done
for f in $BACKUP_SQLITE; do stage_sqlite "$f"; done
for f in $BACKUP_SERVICE_STATE; do stage_path "$f"; done

if [ -n "$BACKUP_HA_CONFIG" ] && [ -d "$BACKUP_HA_CONFIG" ]; then
  mkdir -p "$files$BACKUP_HA_CONFIG"
  # .storage je to nejdulezitejsi v celem adresari: registr entit, uzivatele,
  # tokeny a integrace naklikane v UI. Bez nej je obnovene HA prazdne, i kdyz
  # configuration.yaml sedi. Naopak deps, tts a .cache si HA vyrobi znovu
  # a logy nemaji cenu. Databaze jde zvlast pres sqlite3 nize.
  tar -C "$BACKUP_HA_CONFIG" \
      --exclude=./home-assistant_v2.db \
      --exclude=./home-assistant_v2.db-shm \
      --exclude=./home-assistant_v2.db-wal \
      --exclude=./home-assistant.log \
      --exclude=./home-assistant.log.1 \
      --exclude=./home-assistant.log.fault \
      --exclude=./.ha_run.lock \
      --exclude=./deps \
      --exclude=./tts \
      --exclude=./.cache \
      -cf - . | tar -C "$files$BACKUP_HA_CONFIG" -xf -
  echo "  - $BACKUP_HA_CONFIG [konfigurace vcetne .storage]" >> "$manifest"

  if [ "$BACKUP_HA_HISTORY" = "1" ]; then
    stage_sqlite "$BACKUP_HA_CONFIG/home-assistant_v2.db"
  else
    echo "  - $BACKUP_HA_CONFIG/home-assistant_v2.db [vynechano, BACKUP_HA_HISTORY=0]" >> "$manifest"
  fi
elif [ -n "$BACKUP_HA_CONFIG" ]; then
  fail "BACKUP_HA_CONFIG=$BACKUP_HA_CONFIG neexistuje"
  echo "  - $BACKUP_HA_CONFIG [chybi]" >> "$manifest"
fi

for f in $BACKUP_EXTRA; do stage_path "$f"; done

mkdir -p "$BACKUP_DEST"
chmod 700 "$BACKUP_DEST"

# --- mesice velkych tabulek -------------------------------------------------
# Pracuje se snimkem ve $files, ne zivou databazi: je konzistentni a smi se
# z nej mazat. Mesic odejde, az je den po svem konci (zapisy z posledni
# vteriny mesice uz dorazily), a z nocniho snimku se vypusti jen tehdy, kdyz
# v evidenci sedi pocet radku. Kdyby do uzavreneho mesice pozdeji neco
# pribylo nebo ubylo (proredeni), pocet nesedi a mesic odejde znovu pod
# novym jmenem; starsi kopie v archivu zustava.
export_month() { # snimek tabulka sloupec od do jmeno pocet db
  mdir="$stage/month-$6"
  mkdir -p "$mdir/files"
  mout="$mdir/files/$6.sqlite"
  sqlite3 "$1" "SELECT sql || ';' FROM sqlite_master WHERE tbl_name = '$2' AND sql IS NOT NULL ORDER BY type DESC;" \
    | sqlite3 "$mout" || return 1
  sqlite3 "$mout" "ATTACH '$1' AS s; INSERT INTO main.$2 SELECT * FROM s.$2 WHERE $3 >= $4 AND $3 < $5;" || return 1
  [ "$(sqlite3 "$mout" "SELECT count(*) FROM $2;")" = "$7" ] || return 1
  cat > "$mdir/MANIFEST" <<EOF
Zaloha serveru hodin - mesicni archiv
vytvoreno: $(date -u +%Y-%m-%dT%H:%M:%SZ) UTC
stroj:     $host
tabulka:   $2 z $8
mesic:     $(date -u -d "@$4" +%Y-%m) (UTC), $7 radku

Obnova: nocni zaloha nese $8 bez uzavrenych mesicu teto tabulky.
Po jejim vraceni pridej kazdy mesic (u opakovane odeslaneho staci nejnovejsi):
  sqlite3 $8 "ATTACH 'files/$6.sqlite' AS m; INSERT OR IGNORE INTO $2 SELECT * FROM m.$2;"
EOF
  marchive="$BACKUP_DEST/monthly/$6-$stamp.tar.gz"
  mkdir -p "$BACKUP_DEST/monthly"
  tar -C "$mdir" -czf "$marchive.part" MANIFEST files || return 1
  mv "$marchive.part" "$marchive"
  # Bez odeslani by si Mac stahl kopii, ktera v archivu neni; zitra vznikne znovu.
  { encrypt "$marchive" "$stage/$(basename "$marchive").gpg" \
    && upload "$stage/$(basename "$marchive").gpg" "$BACKUP_ARCHIVE_URL" "$host/monthly/$(basename "$marchive").gpg"; } \
    || { rm -f "$marchive"; return 1; }
  log "mesic do archivu: $host/monthly/$(basename "$marchive").gpg ($7 radku, $(du -h "$marchive" | cut -f1))"
  rm -rf "$mdir" "$stage/$(basename "$marchive").gpg"
}

if [ -n "$BACKUP_MONTHLY" ] && [ -z "$BACKUP_ARCHIVE_URL" ]; then
  log "BACKUP_ARCHIVE_URL chybi: velke tabulky zustavaji v nocni zaloze cele"
fi
vacuum=""
for spec in $BACKUP_MONTHLY; do
  [ -n "$BACKUP_ARCHIVE_URL" ] || break
  db="${spec%%:*}"; rest="${spec#*:}"; table="${rest%%:*}"; col="${rest#*:}"
  snap="$files$db"
  # Bez snimku (databaze chybi nebo selhala) neni z ceho; hlasi to stage_sqlite.
  [ -f "$snap" ] || continue
  first="$(sqlite3 "$snap" "SELECT strftime('%Y-%m', min($col), 'unixepoch') FROM $table;")" \
    || { fail "$db: tabulku $table nejde precist"; continue; }
  [ -n "$first" ] || continue
  base="$(basename "$db")"; base="${base%.*}"
  mkdir -p "$BACKUP_LEDGER/monthly"
  now="$(date -u +%s)"
  m="$first"; last=""
  while :; do
    from="$(date -u -d "$m-01" +%s)"
    next="$(date -u -d "$m-01 +1 month" +%Y-%m)"
    to="$(date -u -d "$next-01" +%s)"
    [ $((to + 86400)) -le "$now" ] || break
    name="$base-$table-$m"
    marker="$BACKUP_LEDGER/monthly/$name"
    count="$(sqlite3 "$snap" "SELECT count(*) FROM $table WHERE $col >= $from AND $col < $to;")"
    if [ ! -f "$marker" ] || [ "$(cat "$marker")" != "$count" ]; then
      [ -f "$marker" ] && log "$name: v archivu $(cat "$marker") radku, ted $count - odchazi znovu"
      if [ "$count" -gt 0 ] && ! export_month "$snap" "$table" "$col" "$from" "$to" "$name" "$count" "$db"; then
        # Tenhle i pozdejsi mesice zustanou v nocni zaloze, zkusi se to zitra.
        fail "$name se nepodarilo odeslat do trvaleho archivu"
        break
      fi
      echo "$count" > "$marker"
    fi
    sqlite3 "$snap" "DELETE FROM $table WHERE $col >= $from AND $col < $to;"
    last="$m"; vacuum="$vacuum $snap"
    m="$next"
  done
  [ -n "$last" ] && echo "  - $db: $table od $first do $last jen v mesicnich archivech ($host/monthly/$name-*)" >> "$manifest"
done
for snap in $(echo "$vacuum" | tr ' ' '\n' | sort -u); do
  sqlite3 "$snap" 'VACUUM;'
done
# Mesicni archivy na stroji jen cekaji, az si je Mac stahne; zdrojem je
# porad ziva databaze a kopie je v trvalem archivu.
find "$BACKUP_DEST/monthly" -name '*.tar.gz' -mtime +90 -delete 2>/dev/null || true

# --- audit ------------------------------------------------------------------
# Nova sluzba nebo statistika, na kterou se v seznamech nahore zapomnelo, se
# ukaze tady a jednotka kvuli ni skonci chybou.
uncovered="$(audit)"
if [ -n "$uncovered" ]; then
  echo >> "$manifest"
  echo "Bez zalohy (doplnit do seznamu v backup.sh nebo BACKUP_IGNORE):" >> "$manifest"
  echo "$uncovered" | sed 's/^/  ! /' >> "$manifest"
  fail "data bez zalohy: $(echo "$uncovered" | tr '\n' ';' | sed 's/;$//')"
fi

archive="$BACKUP_DEST/server-$stamp.tar.gz"
# Do docasneho jmena a teprve pak prejmenovat: stahovac tak nikdy nesebere
# rozepsany archiv.
tar -C "$stage" -czf "$archive.part" MANIFEST files
chmod 600 "$archive.part"
mv "$archive.part" "$archive"

# Prorezani podle stari, ne podle poctu: pri rucnim spusteni behem dne by
# pocitani nejnovejsich ubralo i archivy mladsi nez BACKUP_KEEP_DAYS.
#
# BACKUP_KEEP_MIN je pojistka. Samotne "smaz starsi nez 7 dni" by pri vypadku
# delsim nez tyden smazalo i posledni zalohu, kterou mame - zrovna ve chvili,
# kdy uz zadna nova nevznika. Nejnovejsi kusy proto prezijou bez ohledu na vek.
i=0
ls -1t "$BACKUP_DEST"/server-*.tar.gz 2>/dev/null | while read -r old; do
  i=$((i + 1))
  [ "$i" -le "$BACKUP_KEEP_MIN" ] && continue
  if [ -n "$(find "$old" -mtime +"$BACKUP_KEEP_DAYS" 2>/dev/null)" ]; then
    log "maze starou zalohu $(basename "$old")"
    rm -f "$old"
  fi
done

log "hotovo: $archive ($(du -h "$archive" | cut -f1))"

# --- kopie mimo stroj --------------------------------------------------------
# Nahrava se az po prorezani, takze selhane nahrani nezastavi mistni zalohu;
# jednotka pak ale skonci chybou a OnFailure= posle push.
if [ -n "$BACKUP_OFFSITE_URL" ]; then
  encrypted="$stage/$(basename "$archive").gpg"
  if encrypt "$archive" "$encrypted" \
     && upload "$encrypted" "$BACKUP_OFFSITE_URL" "$host/$(basename "$encrypted")"; then
    log "mimo stroj: $host/$(basename "$encrypted") ($(du -h "$encrypted" | cut -f1), klic ${fingerprint#????????????????????????})"
  else
    fail "kopii mimo stroj se nepodarilo odeslat"
  fi
fi

# --- fotky do trvaleho archivu ----------------------------------------------
# Kazda jednou. Fotky nejsou tajne (obrazky z e-shopu), takze se nesifruji -
# bucket je beztak soukromy. Evidence se prepisuje po kazde fotce, takze
# preruseny beh nic neodesle dvakrat a nic nevynecha.
if [ -n "$BACKUP_ARCHIVE_URL" ]; then
  mkdir -p "$BACKUP_LEDGER/photos"
  for dir in $BACKUP_PHOTOS; do
    [ -d "$dir" ] || continue
    slug="$(echo "${dir#/}" | tr '/' '_')"
    ledger="$BACKUP_LEDGER/photos/$slug"
    touch "$ledger"
    ls -1 "$dir" | sort > "$stage/photos-now"
    sort -u "$ledger" > "$stage/photos-done"
    comm -23 "$stage/photos-now" "$stage/photos-done" > "$stage/photos-todo"
    todo="$(wc -l < "$stage/photos-todo" | tr -d ' ')"
    sent=0; failed=0
    while read -r photo; do
      [ "$sent" -ge "$BACKUP_PHOTOS_PER_RUN" ] && break
      # hlidac ji mezitim mohl smazat
      [ -f "$dir/$photo" ] || continue
      if curl -fsS --retry 2 --retry-delay 5 --max-time 120 -o /dev/null \
           -T "$dir/$photo" "${BACKUP_ARCHIVE_URL%/}/$host/photos/$slug/$photo"; then
        echo "$photo" >> "$ledger"
        sent=$((sent + 1))
      else
        failed=$((failed + 1))
        # Nejspis vyprsel PAR nebo nejede sit; zbytek zkusi dalsi noc.
        [ "$failed" -ge 5 ] && break
      fi
    done < "$stage/photos-todo"
    [ "$todo" -gt 0 ] && log "fotky $dir: odeslano $sent z $todo novych"
    [ "$failed" -eq 0 ] || fail "fotky $dir: $failed nahrani selhalo"
  done
fi

exit "$rc"
