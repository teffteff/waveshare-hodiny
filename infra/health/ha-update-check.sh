#!/usr/bin/env bash
# Tydenni kontrola, jestli pro Home Assistant nevysla nova verze (ha-update.timer).
# HA jede z tagu :stable a sam se nepovysi; tohle jen posle push, kdyz je
# :stable novejsi nez bezici kontejner, a to jednou pro kazdou verzi. Povyseni
# samo zustava rucni: tools/update-ha.sh --apply, po precteni poznamek k vydani.
#
# Bezi jako root, protoze se pta Dockeru. Stazeny image se neztrati: --apply
# pak uz nic nestahuje. Starsi :stable, ktery tim prestane mit tag a nebezi,
# se hned uklidi, aby na disku nelezelo 2,3 GB za kazdou verzi.
set -euo pipefail

NAME=homeassistant
IMAGE=ghcr.io/home-assistant/home-assistant:stable
STATE="${HA_UPDATE_STATE:-/var/lib/ha-update}/notified"

version_of() { docker image inspect "$1" --format '{{index .Config.Labels "org.opencontainers.image.version"}}'; }

running_id="$(docker inspect "$NAME" --format '{{.Image}}')"
running="$(version_of "$running_id")"
docker pull --quiet "$IMAGE" >/dev/null
docker image prune -f >/dev/null
new_id="$(docker image inspect "$IMAGE" --format '{{.Id}}')"
new="$(version_of "$new_id")"

if [ "$new_id" = "$running_id" ]; then
  echo "Home Assistant $running je aktualni"
  exit 0
fi
if [ -f "$STATE" ] && [ "$(cat "$STATE")" = "$new" ]; then
  echo "Home Assistant $new uz nahlaseny (bezi $running)"
  exit 0
fi
series="$(printf '%s' "$new" | cut -d. -f1,2 | tr -d .)"
/usr/bin/python3.11 /opt/health/check.py --notify "Home Assistant $new je venku" \
  "Bezi $running. Poznamky k vydani (Backward-incompatible changes): https://www.home-assistant.io/blog/categories/release-notes/ (serie $series). Povyseni z Macu: tools/update-ha.sh --apply"
printf '%s\n' "$new" > "$STATE"
echo "nahlaseno: $new (bezi $running)"
