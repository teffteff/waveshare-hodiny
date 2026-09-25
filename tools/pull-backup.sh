#!/usr/bin/env bash
# Stáhne nejnovější zálohu ze serveru sem, na tenhle stroj. Teprve tímhle
# krokem začne mít noční backup.timer smysl — archiv ležící na stejném disku
# jako originál neochrání před ničím kromě vlastního rm.
#
#   tools/pull-backup.sh              stáhne nejnovější archiv
#   tools/pull-backup.sh --run        nejdřív spustí zálohu na serveru, pak stáhne
#   tools/pull-backup.sh --list       jen vypíše, co na serveru leží
#
# Kam se stahuje: BACKUP_LOCAL_DIR z .env, jinak ~/waveshare-zalohy.
# Archiv nese hesla a klíče v otevřené podobě, takže nepatří do repozitáře
# ani do sdílené složky; stahuje se s právy 600.
#
# Stáhne i měsíční archivy velkých tabulek (monthly/) a nové fotky hlídačů
# (photos/); ty v nočním archivu nejsou.
#
# Návratový kód 0 = staženo a ověřeno, 1 = chyba.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -f "$REPO_ROOT/.env" ]; then
  while IFS='=' read -r key value; do
    case "$key" in
      CLOCK_SSH|CLOCK_SSH_KEY|BACKUP_LOCAL_DIR)
        # eval kvůli $HOME v cestě; hodnoty pocházejí z vlastního .env.
        [ -z "${!key:-}" ] && eval "$key=\"$value\""
        ;;
    esac
  done < <(grep -E '^(CLOCK_SSH|CLOCK_SSH_KEY|BACKUP_LOCAL_DIR)=' "$REPO_ROOT/.env")
fi

SSH_TARGET="${CLOCK_SSH:-}"
SSH_KEY="${CLOCK_SSH_KEY:-}"
LOCAL_DIR="${BACKUP_LOCAL_DIR:-$HOME/waveshare-zalohy}"
REMOTE_DIR="${BACKUP_DEST:-/opt/backup/data}"

if [ -z "$SSH_TARGET" ] || [ -z "$SSH_KEY" ]; then
  printf 'Chybí CLOCK_SSH nebo CLOCK_SSH_KEY. Doplň do .env v kořeni repozitáře:\n' >&2
  printf '  CLOCK_SSH=uzivatel@1.2.3.4\n' >&2
  printf '  CLOCK_SSH_KEY=$HOME/cesta/ke/klici.key\n' >&2
  exit 2
fi

SSH_OPTS=(-i "$SSH_KEY" -o ConnectTimeout=15)
ssh_run() { ssh "${SSH_OPTS[@]}" "$SSH_TARGET" "$@"; }

case "${1:-}" in
  --list)
    ssh_run "sudo sh -c 'ls -lh $REMOTE_DIR/'" || exit 1
    exit 0
    ;;
  --run)
    printf 'Spouštím zálohu na serveru…\n'
    # --no-block by skončil hned a nevěděli bychom, jestli prošla.
    if ! ssh_run "sudo systemctl start backup.service"; then
      printf 'Záloha na serveru selhala, podrobnosti:\n' >&2
      ssh_run "sudo journalctl -u backup.service -n 30 --no-pager" >&2
      exit 1
    fi
    ;;
  "") ;;
  *)
    printf 'Neznámý přepínač: %s (znám --run, --list)\n' "$1" >&2
    exit 2
    ;;
esac

# Hvězdičku musí rozbalit až root uvnitř sudo: adresář má práva 700, takže
# přihlášenému uživateli by glob zůstal nerozbalený a ls by nenašel nic.
newest="$(ssh_run "sudo sh -c 'ls -1t $REMOTE_DIR/server-*.tar.gz 2>/dev/null | head -1'")"
if [ -z "$newest" ]; then
  printf 'Na serveru v %s žádný archiv není. Běžela už backup.service?\n' "$REMOTE_DIR" >&2
  exit 1
fi

name="$(basename "$newest")"
mkdir -p "$LOCAL_DIR"
chmod 700 "$LOCAL_DIR"
# Archivy nesou hesla v otevřené podobě, takže nemají co dělat na záložním
# disku Time Machine — ten bývá nešifrovaný a odnáší se z domu. Vyloučení je
# xattr na adresáři, ne nastavení Time Machine: zmizí, když adresář někdo smaže
# a založí znovu, proto se kontroluje při každém běhu, ne jednorázově.
if command -v tmutil >/dev/null 2>&1; then
  if ! tmutil isexcluded "$LOCAL_DIR" 2>/dev/null | grep -q 'Excluded'; then
    tmutil addexclusion "$LOCAL_DIR" 2>/dev/null \
      && printf 'Adresář %s vyloučen ze záloh Time Machine.\n' "$LOCAL_DIR"
  fi
fi
target="$LOCAL_DIR/$name"

if [ -f "$target" ]; then
  printf '%s už tady je, nestahuji znovu.\n' "$name"
else
  printf 'Stahuji %s…\n' "$name"
  # Archiv patří rootovi, takže scp na něj nedosáhne — čte ho sudo cat na
  # druhé straně. Do .part a až pak přejmenovat, ať se přerušené stahování
  # nedá splést s hotovým archivem.
  umask 077
  if ! ssh_run "sudo cat '$newest'" > "$target.part"; then
    printf 'Stahování selhalo.\n' >&2
    rm -f "$target.part"
    exit 1
  fi
  mv "$target.part" "$target"
fi

# Ověření, že to, co dorazilo, jde rozbalit — jinak se na chybu přijde až
# ve chvíli, kdy je server pryč a záloha je jediné, co zbylo.
if ! tar -tzf "$target" >/dev/null 2>&1; then
  printf 'POZOR: %s se nedá rozbalit, archiv je poškozený.\n' "$name" >&2
  exit 1
fi

printf '\n%s (%s)\n' "$target" "$(du -h "$target" | cut -f1)"
printf -- '--- MANIFEST ---\n'
tar -xzf "$target" -O MANIFEST 2>/dev/null || printf '(MANIFEST v archivu chybí)\n'

# Prořezání stejně jako na serveru: podle stáří, s pojistkou na nejnovější
# kusy. Bez ní by po týdnu bez spuštění zmizela i poslední stažená záloha.
keep_days="${BACKUP_LOCAL_KEEP_DAYS:-7}"
keep_min="${BACKUP_LOCAL_KEEP_MIN:-3}"
i=0
ls -1t "$LOCAL_DIR"/server-*.tar.gz 2>/dev/null | while read -r old; do
  i=$((i + 1))
  [ "$i" -le "$keep_min" ] && continue
  if [ -n "$(find "$old" -mtime +"$keep_days" 2>/dev/null)" ]; then
    printf 'mažu starou zálohu %s\n' "$(basename "$old")"
    rm -f "$old"
  fi
done

status=0

# Měsíční archivy velkých tabulek (radar). Noční záloha je nenese, takže se
# tady nikdy neprořezávají - každý je jediný kus svého měsíce. Stahuje se
# všechno, co tu ještě není; server je drží 90 dní.
mkdir -p "$LOCAL_DIR/monthly"
ssh_run "sudo sh -c 'ls -1 $REMOTE_DIR/monthly/*.tar.gz 2>/dev/null'" | while read -r remote; do
  m="$LOCAL_DIR/monthly/$(basename "$remote")"
  [ -f "$m" ] && continue
  printf 'Stahuji měsíční archiv %s…\n' "$(basename "$remote")"
  if ssh_run "sudo cat '$remote'" > "$m.part" && tar -tzf "$m.part" >/dev/null 2>&1; then
    mv "$m.part" "$m"
  else
    printf 'POZOR: měsíční archiv %s se nestáhl celý.\n' "$(basename "$remote")" >&2
    rm -f "$m.part"
    exit 1
  fi
done || status=1

# Fotky hlídačů: jen přibývají. --ignore-existing, bez --delete - fotka, kterou
# hlídač po 120 dnech smaže, tady zůstane. Soubory patří uživateli watch,
# proto rsync na druhé straně pod sudo.
for dir in ${PULL_PHOTOS:-/var/lib/watch/cache/images /var/lib/ou-watch/cache/images}; do
  slug="$(printf '%s' "${dir#/}" | tr '/' '_')"
  mkdir -p "$LOCAL_DIR/photos/$slug"
  if ! rsync -rt --ignore-existing --rsync-path="sudo rsync" \
       -e "ssh -i $SSH_KEY -o ConnectTimeout=15" \
       "$SSH_TARGET:$dir/" "$LOCAL_DIR/photos/$slug/"; then
    printf 'POZOR: fotky z %s se nestáhly.\n' "$dir" >&2
    status=1
  fi
done
printf 'Fotky: %s souborů v %s/photos\n' \
  "$(find "$LOCAL_DIR/photos" -type f | wc -l | tr -d ' ')" "$LOCAL_DIR"

exit "$status"
