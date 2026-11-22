#!/usr/bin/env bash
# Weekly job for tools/upstream_watch.py (CooLajz + MeteoPlaneRadar news). Runs on the Beelink (always on),
# from its clone ~/waveshare-hodiny, as a cron line like the rodokmen skill watcher.
#     tools/install-upstream-watch.sh              install / replace (Monday 07:50)
#     tools/install-upstream-watch.sh --status     cron line, watcher state, end of the log
#     tools/install-upstream-watch.sh --uninstall  remove
# On a Mac the same commands remove the old launchd job (cz.majnr.hodiny.upstream-watch) instead.
# Reports: ~/notes/projects/hodiny-upstream/<date>.md, committed and pushed to the notes repo
# (--commit-notes). Nothing is ever integrated automatically. ntfy: ~/.config/hodiny-upstream/env
# with UPSTREAM_NTFY_TOPIC=... (optional; clock secrets stay off the Beelink).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
LOG="$HOME/hodiny-upstream-watch.log"
MARK="# hodiny-upstream-watch"

if [[ "$(uname)" == "Darwin" ]]; then
  LABEL="cz.majnr.hodiny.upstream-watch"
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
  echo "Mac: launchd job removed (the watcher runs on the Beelink: ssh beelink ~/waveshare-hodiny/tools/install-upstream-watch.sh)"
  exit 0
fi

case "${1:-}" in
  --status) crontab -l 2>/dev/null | grep -F "$MARK" || echo "not installed"
            python3 tools/upstream_watch.py --status || true
            tail -n 20 "$LOG" 2>/dev/null || true; exit 0 ;;
  --uninstall) (crontab -l 2>/dev/null | grep -vF "$MARK" || true) | crontab -; echo "removed"; exit 0 ;;
  "") ;;
  *) echo "usage: $0 [--status|--uninstall]" >&2; exit 2 ;;
esac
PY="$(command -v python3)" || { echo "python3 not found" >&2; exit 1; }
CLAUDE="$(command -v claude)" || { echo "claude (Claude Code) not found in PATH" >&2; exit 1; }
command -v gh >/dev/null || echo "warning: gh missing - reports without release notes" >&2
[[ -d "$HOME/notes/.git" ]] || { echo "~/notes is not a git clone" >&2; exit 1; }
[[ -f "$HOME/.config/hodiny-upstream/env" ]] || echo "note: no ~/.config/hodiny-upstream/env - reports without ntfy" >&2
RUN_PATH="$(dirname "$PY"):$(dirname "$CLAUDE"):/usr/local/bin:/usr/bin:/bin"
LINE="50 7 * * 1 cd $PWD && git pull -q --ff-only origin main; PATH=$RUN_PATH HOME=$HOME $PY tools/upstream_watch.py --commit-notes >> $LOG 2>&1 $MARK"
(crontab -l 2>/dev/null | grep -vF "$MARK" || true; echo "$LINE") | crontab -
echo "installed: $LINE"
