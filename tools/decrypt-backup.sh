#!/usr/bin/env bash
# Rozšifruje kopii zálohy stažené z OCI Object Storage (server-….tar.gz.gpg)
# na obyčejný archiv, stejný, jaký leží v /opt/backup/data a ~/waveshare-zalohy.
#
#   tools/decrypt-backup.sh server-20260923-032000.tar.gz.gpg        → vedle bez .gpg
#   tools/decrypt-backup.sh soubor.gpg cil.tar.gz
#
# Soukromý klíč bere z HODINY_BACKUP_SECRET, jinak z
# ~/.config/hodiny-backup/offsite-secret.asc (záloha v ~/Documents/oracleKeys).
# Klíč se importuje do dočasné klíčenky, která se po skončení smaže, takže
# běžný GnuPG na Macu zůstane nedotčený. Potřebuje gpg (brew install gnupg).
# Obnova z archivu: „Zálohy dat“ v infra/README.md.

set -euo pipefail

in="${1:-}"
[ -f "$in" ] || { sed -n '2,12p' "$0"; exit 1; }
out="${2:-${in%.gpg}}"
[ "$out" != "$in" ] || out="$in.tar.gz"
secret="${HODINY_BACKUP_SECRET:-$HOME/.config/hodiny-backup/offsite-secret.asc}"
[ -f "$secret" ] || { echo "Chybí soukromý klíč $secret" >&2; exit 1; }
command -v gpg >/dev/null || { echo "Chybí gpg: brew install gnupg" >&2; exit 1; }

keyring="$(mktemp -d)"
trap 'gpgconf --homedir "$keyring" --kill all 2>/dev/null || true; rm -rf "$keyring"' EXIT
chmod 700 "$keyring"
gpg -q --homedir "$keyring" --batch --import "$secret" 2>/dev/null
( umask 077; gpg -q --homedir "$keyring" --batch --output "$out" --decrypt "$in" )
tar -tzf "$out" MANIFEST >/dev/null || { echo "Rozšifrováno, ale není to archiv zálohy: $out" >&2; exit 1; }
echo "hotovo: $out"
tar -xzOf "$out" MANIFEST | head -3
