#!/bin/sh
# Zaloha toho, co na serveru neni v zadnem repozitari: konfigurace Home
# Assistanta, stavove databaze hlidacu a soubory s hesly. Kod sluzeb se
# zalohovat nemusi, ten je v infra/ a v repozitarich hlidacu; obnova stroje
# je popsana v infra/README.md, oddil "Obnova serveru".
#
# Archiv na stroji je jen mezistupen. Smysl dostane az tim, ze si ho nekdo
# stahne pryc - tools/pull-backup.sh. Zaloha na stejnem disku neochrani pred
# nicim krome vlastniho rm.
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
: "${BACKUP_SECRETS=/etc/caddy/caddy.env /opt/news/news.env /opt/agenda/agenda.env /opt/agenda/key.json /opt/school/school.env /opt/watch/watch.env /opt/ou-watch/watch.env}"
# Databaze, ktere se za behu zapisuji. Kopiruji se pres sqlite3 .backup, ne
# cp: prosty cp za behu utrhne stranku uprostred transakce a vysledek je
# nepouzitelny. Hlidaci si v nich drzi, co uz videli.
: "${BACKUP_SQLITE=/opt/watch/state.db /opt/ou-watch/state.db}"
# Cokoli navic, oddelene mezerami. Svety Minecraftu sem patri jen tehdy, kdyz
# je na ne dost mista - jsou radove GB a meni se porad.
: "${BACKUP_EXTRA:=}"

log() { echo "backup: $*"; }

stamp="$(date -u +%Y%m%d-%H%M%S)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT INT TERM

files="$stage/files"
manifest="$stage/MANIFEST"
mkdir -p "$files"

{
  echo "Zaloha serveru hodin"
  echo "vytvoreno: $(date -u +%Y-%m-%dT%H:%M:%SZ) UTC"
  echo "stroj:     $(hostname)"
  echo
  echo "Obnova: rozbal a vrat soubory z files/ na stejne cesty, prava"
  echo "podle infra/README.md (hesla 600, key.json 600). Kod sluzeb sem"
  echo "nepatri, ten se nasazuje z repozitare."
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
      log "VAROVANI: $src se zkopiroval, ale integrity_check rekl '$check'"
      echo "  - $src [sqlite, integrity_check: $check]" >> "$manifest"
    fi
  else
    log "VAROVANI: sqlite zalohu $src se nepodarilo porizdit: $(cat "$stage/sqlite.err")"
    echo "  - $src [SELHALO]" >> "$manifest"
    rm -f "$out"
  fi
}

for f in $BACKUP_SECRETS; do stage_path "$f"; done
for f in $BACKUP_SQLITE; do stage_sqlite "$f"; done

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
  log "VAROVANI: BACKUP_HA_CONFIG=$BACKUP_HA_CONFIG neexistuje"
  echo "  - $BACKUP_HA_CONFIG [chybi]" >> "$manifest"
fi

for f in $BACKUP_EXTRA; do stage_path "$f"; done

mkdir -p "$BACKUP_DEST"
chmod 700 "$BACKUP_DEST"
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
