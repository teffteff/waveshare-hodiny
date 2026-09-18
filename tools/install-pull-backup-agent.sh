#!/usr/bin/env bash
# Nastaví na tomhle Macu launchd agenta, který jednou denně stáhne zálohu ze
# serveru (tools/pull-backup.sh). Bez něj noční backup.timer na serveru chrání
# jen před vlastním rm — archiv leží na stejném disku jako originál.
#
#   tools/install-pull-backup-agent.sh              nainstaluje a spustí
#   tools/install-pull-backup-agent.sh --uninstall  odebere
#   tools/install-pull-backup-agent.sh --status      řekne, jak si agent stojí
#
# Plist se generuje tady, místo aby ležel v repozitáři: nese absolutní cestu
# k tomuhle klonu, která je na každém stroji jiná (stejně jako {{DOMAIN}}
# v Caddyfile).
#
# Když Mac ve smluvenou hodinu spí nebo je vypnutý, launchd úlohu spustí
# jednou hned, jak se stroj probudí. „Jednou denně, když Mac běží“ tedy sedí
# i pro stroj, který přes noc nesvítí.

set -uo pipefail

LABEL="cz.majnr.hodiny.pull-backup"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/hodiny-pull-backup.log"
# Hodina, kdy se stahuje. Server sype archiv ve 03:20 UTC (05:20 letního
# středoevropského času), takže dopoledne je vždycky hotový.
HOUR="${PULL_BACKUP_HOUR:-10}"
MINUTE="${PULL_BACKUP_MINUTE:-0}"

case "${1:-}" in
  --uninstall)
    launchctl bootout "gui/$UID/$LABEL" 2>/dev/null
    rm -f "$PLIST"
    printf 'Agent %s odebrán.\n' "$LABEL"
    printf 'Stažené zálohy v ~/waveshare-zalohy zůstaly, smaž je ručně.\n'
    exit 0
    ;;
  --status)
    if [ ! -f "$PLIST" ]; then
      printf 'Agent není nainstalovaný (%s chybí).\n' "$PLIST"
      exit 1
    fi
    launchctl print "gui/$UID/$LABEL" 2>/dev/null \
      | grep -E 'state|last exit code|runs =' || printf 'launchd o agentovi neví.\n'
    printf '\nPoslední řádky logu (%s):\n' "$LOG"
    tail -5 "$LOG" 2>/dev/null || printf '(log zatím prázdný)\n'
    exit 0
    ;;
  "") ;;
  *)
    printf 'Neznámý přepínač: %s (znám --uninstall, --status)\n' "$1" >&2
    exit 2
    ;;
esac

if [ ! -x "$REPO_ROOT/tools/pull-backup.sh" ]; then
  printf 'Chybí spustitelný %s/tools/pull-backup.sh\n' "$REPO_ROOT" >&2
  exit 1
fi
if [ ! -f "$REPO_ROOT/.env" ]; then
  printf 'Chybí %s/.env s CLOCK_SSH a CLOCK_SSH_KEY — agent by neměl kam sáhnout.\n' "$REPO_ROOT" >&2
  exit 1
fi

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"

cat > "$PLIST" <<PLIST_END
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$REPO_ROOT/tools/pull-backup.sh</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$REPO_ROOT</string>
  <!-- launchd dává úlohám holé prostředí, ne to z .zshrc. Bez PATH by skript
       nenašel ssh ani tar. -->
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Hour</key>
    <integer>$HOUR</integer>
    <key>Minute</key>
    <integer>$MINUTE</integer>
  </dict>
  <!-- Ne při každém přihlášení; na zameškaný čas stačí, že ho launchd dožene
       po probuzení. -->
  <key>RunAtLoad</key>
  <false/>
  <key>StandardOutPath</key>
  <string>$LOG</string>
  <key>StandardErrorPath</key>
  <string>$LOG</string>
  <!-- Nice: stahování je na pozadí, ať nepřekáží. -->
  <key>Nice</key>
  <integer>5</integer>
</dict>
</plist>
PLIST_END

# bootout před bootstrap: bez toho druhá instalace skončí na "service already
# loaded" a plist by zůstal ten starý.
launchctl bootout "gui/$UID/$LABEL" 2>/dev/null
if ! launchctl bootstrap "gui/$UID" "$PLIST"; then
  printf 'launchctl bootstrap selhal. Plist zůstal v %s.\n' "$PLIST" >&2
  exit 1
fi

printf 'Agent %s nainstalován.\n' "$LABEL"
printf '  spouští:  %s/tools/pull-backup.sh\n' "$REPO_ROOT"
printf '  kdy:      každý den v %02d:%02d (po probuzení i zpětně)\n' "$HOUR" "$MINUTE"
printf '  log:      %s\n' "$LOG"
printf '\nZkouška teď hned:  launchctl kickstart -p gui/%s/%s\n' "$UID" "$LABEL"
printf 'Jak si stojí:      tools/install-pull-backup-agent.sh --status\n'
