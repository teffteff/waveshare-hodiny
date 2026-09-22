#!/usr/bin/env bash
# Aktualizace Home Assistanta na serveru. Kontejner jede z tagu :stable a sám
# se nepovýší — dnf-automatic Docker nevidí, takže bez tohohle skriptu zůstává
# HA na verzi z doby, kdy se kontejner naposledy zakládal.
#
#   tools/update-ha.sh           stáhne nejnovější :stable a řekne, jestli je
#                                novější než běžící; na běžící HA nesáhne
#   tools/update-ha.sh --apply   záloha, nový kontejner, čeká na HA; když do
#                                pěti minut neodpoví 200, vrátí předchozí image
#
# Návrat na předchozí image nevrátí data: nová verze mohla při startu převést
# databázi nebo .storage. Proto --apply nejdřív spustí backup.service a čerstvý
# archiv z /opt/backup/data je to, z čeho se obnovuje, kdyby zpětný krok
# nestačil (postup v „Zálohy dat“ v infra/README.md).
#
# Před --apply si přečti poznámky k vydání (Breaking changes):
#   https://www.home-assistant.io/blog/categories/release-notes/
#
# Návratový kód 0 = aktuální nebo povýšeno, 1 = chyba, 3 = je nová verze
# (jen bez --apply).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
APPLY=0
case "${1:-}" in
  --apply) APPLY=1 ;;
  "") ;;
  *) sed -n '2,21p' "$0"; exit 1 ;;
esac

[ -f .env ] && { set -a; . ./.env; set +a; }
: "${CLOCK_SSH:?Chybí CLOCK_SSH v .env}"
: "${CLOCK_SSH_KEY:?Chybí CLOCK_SSH_KEY v .env}"
SSH=(ssh -i "$CLOCK_SSH_KEY" -o ConnectTimeout=15
     -o BatchMode=yes -o LogLevel=ERROR "$CLOCK_SSH")

"${SSH[@]}" "sudo APPLY=$APPLY bash -s" <<'EOF'
set -euo pipefail
NAME=homeassistant
IMAGE=ghcr.io/home-assistant/home-assistant:stable

version_of() { docker image inspect "$1" --format '{{index .Config.Labels "org.opencontainers.image.version"}}'; }

current_id="$(docker inspect "$NAME" --format '{{.Image}}')"
current_version="$(version_of "$current_id")"
# Konfigurace se bere z běžícího kontejneru, ne z návodu: cesta k ní je
# doopravdy /path/to/your/config (viz infra/README.md) a nemá se tipovat.
config_dir="$(docker inspect "$NAME" --format '{{range .Mounts}}{{if eq .Destination "/config"}}{{.Source}}{{end}}{{end}}')"
[ -n "$config_dir" ] || { echo "kontejner nemá bind-mount /config" >&2; exit 1; }

echo "běží: $current_version"
docker pull --quiet "$IMAGE" >/dev/null
new_id="$(docker image inspect "$IMAGE" --format '{{.Id}}')"
new_version="$(version_of "$new_id")"
echo ":stable: $new_version"

if [ "$new_id" = "$current_id" ]; then
  echo "HA je aktuální"
  exit 0
fi
if [ "$APPLY" != 1 ]; then
  echo "je nová verze; povýšení: tools/update-ha.sh --apply"
  exit 3
fi

run_ha() {
  # Stejné parametry jako v „Zabezpečení stroje“: bez --privileged, síť hostitele.
  docker run -d --name "$NAME" --restart=unless-stopped \
    --network=host -e TZ=Europe/Prague \
    -v "$config_dir:/config" "$1" >/dev/null
}
wait_ha() {
  for _ in $(seq 60); do
    code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 http://127.0.0.1:8123/ || true)"
    [ "$code" = 200 ] && return 0
    sleep 5
  done
  return 1
}

echo "záloha (backup.service)…"
systemctl start backup.service
docker tag "$current_id" "homeassistant-previous:$current_version"

echo "nový kontejner $new_version…"
docker stop "$NAME" >/dev/null
docker rm "$NAME" >/dev/null
run_ha "$IMAGE"
if wait_ha; then
  echo "HA $new_version odpovídá 200"
  # Pro návrat zůstává jen image, ze kterého se právě povyšovalo. Starší
  # homeassistant-previous:* by prune nesmazal (mají tag) a každý měsíc by
  # přibylo 2,3 GB.
  docker images --format '{{.Repository}}:{{.Tag}}' homeassistant-previous \
    | grep -vx "homeassistant-previous:$current_version" \
    | xargs -r docker rmi >/dev/null 2>&1 || true
  docker image prune -f >/dev/null
  exit 0
fi

echo "HA $new_version do pěti minut neodpověděl, vracím $current_version" >&2
docker logs --tail 30 "$NAME" >&2 || true
docker stop "$NAME" >/dev/null || true
docker rm "$NAME" >/dev/null || true
run_ha "homeassistant-previous:$current_version"
if wait_ha; then
  echo "vráceno na $current_version; kdyby něco chybělo, obnov .storage z nejnovějšího archivu v /opt/backup/data" >&2
else
  echo "ani $current_version neodpovídá — obnov konfiguraci z /opt/backup/data" >&2
fi
exit 1
EOF
