#!/usr/bin/env bash
# Přeloží a spustí všechny testy běžící na počítači. Nevyžaduje arduino-cli
# ani připojené hodiny, takže je to nejrychlejší kontrola před sestavením.
set -uo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/WaveshareHodiny"
SHIM_DIR="$ROOT_DIR/tools/hostshim"
BUILD_DIR="${TMPDIR:-/tmp}/waveshare-hodiny-host-tests"
CXX_BIN="${CXX:-c++}"

mkdir -p "$BUILD_DIR"

# Název testu -> zdrojové soubory firmwaru, které potřebuje k sestavení.
run_test() {
  local name="$1"
  shift
  local sources=("$ROOT_DIR/tools/test_$name.cpp" "$@")
  printf '%-24s ' "$name"
  if ! "$CXX_BIN" -std=c++17 -Wall -I "$SHIM_DIR" -I "$FIRMWARE_DIR" \
      -o "$BUILD_DIR/$name" "${sources[@]}" > "$BUILD_DIR/$name.log" 2>&1; then
    echo "PŘEKLAD SELHAL"
    sed 's/^/    /' "$BUILD_DIR/$name.log"
    return 1
  fi
  if ! "$BUILD_DIR/$name" > "$BUILD_DIR/$name.out" 2>&1; then
    echo "SELHAL"
    sed 's/^/    /' "$BUILD_DIR/$name.out"
    return 1
  fi
  echo "OK"
  return 0
}

failures=0
run_test wifi_provisioning "$FIRMWARE_DIR/WifiProvisioning.cpp" || failures=$((failures + 1))
run_test device_name "$FIRMWARE_DIR/DeviceName.cpp" || failures=$((failures + 1))
run_test day_night_logic "$FIRMWARE_DIR/DayNightLogic.cpp" "$FIRMWARE_DIR/Astronomy.cpp" || failures=$((failures + 1))
run_test astronomy "$FIRMWARE_DIR/Astronomy.cpp" || failures=$((failures + 1))
run_test screen_schedule "$FIRMWARE_DIR/ScreenSchedule.cpp" "$FIRMWARE_DIR/Astronomy.cpp" || failures=$((failures + 1))
run_test semver "$FIRMWARE_DIR/SemVer.cpp" || failures=$((failures + 1))
run_test tmep_parser "$FIRMWARE_DIR/TmepParser.cpp" || failures=$((failures + 1))
run_test home_assistant_connection_policy || failures=$((failures + 1))
run_test clock_config "$FIRMWARE_DIR/ClockConfig.cpp" || failures=$((failures + 1))
run_test value_slot_form "$FIRMWARE_DIR/ConfigurationForm.cpp" "$FIRMWARE_DIR/ClockConfig.cpp" || failures=$((failures + 1))
run_test settings_backup "$FIRMWARE_DIR/SettingsBackup.cpp" "$FIRMWARE_DIR/ClockConfig.cpp" || failures=$((failures + 1))
# Šifrování zálohy potřebuje mbedTLS 3 stejně jako firmware (brew install mbedtls@3).
MBEDTLS_DIR="${MBEDTLS_DIR:-$(brew --prefix mbedtls@3 2>/dev/null)}"
if [[ -n "$MBEDTLS_DIR" && -f "$MBEDTLS_DIR/include/mbedtls/gcm.h" ]]; then
  run_test settings_backup_crypto "$FIRMWARE_DIR/SettingsBackupCrypto.cpp" \
    "$FIRMWARE_DIR/SettingsBackup.cpp" "$FIRMWARE_DIR/ClockConfig.cpp" \
    "$FIRMWARE_DIR/JsonScan.cpp" -I "$MBEDTLS_DIR/include" \
    "$MBEDTLS_DIR/lib/libmbedcrypto.a" || failures=$((failures + 1))
else
  printf '%-24s %s\n' settings_backup_crypto "CHYBÍ mbedTLS 3 (MBEDTLS_DIR)"
  failures=$((failures + 1))
fi
run_test rss_parser "$FIRMWARE_DIR/RssParser.cpp" || failures=$((failures + 1))
run_test clock_namedays "$FIRMWARE_DIR/ClockNamedays.cpp" || failures=$((failures + 1))
run_test http_body_reader "$FIRMWARE_DIR/HttpBodyReader.cpp" || failures=$((failures + 1))
run_test rain_viewer_index "$FIRMWARE_DIR/RainViewerIndex.cpp" || failures=$((failures + 1))
run_test weather_forecast "$FIRMWARE_DIR/WeatherForecast.cpp" || failures=$((failures + 1))
run_test weather_forecast_layout || failures=$((failures + 1))
run_test chmi_frame_names "$FIRMWARE_DIR/ChmiFrameNames.cpp" || failures=$((failures + 1))
run_test map_canvas "$FIRMWARE_DIR/MapCanvas.cpp" || failures=$((failures + 1))
run_test plane_feed_url "$FIRMWARE_DIR/PlaneFeedUrl.cpp" || failures=$((failures + 1))
run_test rss_feed_url "$FIRMWARE_DIR/RssFeedUrl.cpp" || failures=$((failures + 1))
run_test adsb_parser "$FIRMWARE_DIR/AdsbParser.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test route_parser "$FIRMWARE_DIR/RouteParser.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test agenda_parser "$FIRMWARE_DIR/AgendaParser.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test agenda_layout || failures=$((failures + 1))
run_test school_parser "$FIRMWARE_DIR/SchoolParser.cpp" "$FIRMWARE_DIR/AgendaParser.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test lightning_feed "$FIRMWARE_DIR/LightningFeed.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test satellite_feed "$FIRMWARE_DIR/SatelliteFeed.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test rain_alert "$FIRMWARE_DIR/RainAlert.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test push_alerts "$FIRMWARE_DIR/PushAlerts.cpp" || failures=$((failures + 1))
run_test weather_warnings "$FIRMWARE_DIR/WeatherWarnings.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test sky_feed "$FIRMWARE_DIR/SkyFeed.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))
run_test crash_log "$FIRMWARE_DIR/CrashLog.cpp" || failures=$((failures + 1))
run_test remote_admin "$FIRMWARE_DIR/RemoteAdmin.cpp" || failures=$((failures + 1))
run_test sky_render "$FIRMWARE_DIR/SkyRender.cpp" "$FIRMWARE_DIR/SkyCanvas.cpp" "$FIRMWARE_DIR/SkyFeed.cpp" "$FIRMWARE_DIR/SatelliteFeed.cpp" "$FIRMWARE_DIR/MapCanvas.cpp" "$FIRMWARE_DIR/JsonScan.cpp" || failures=$((failures + 1))

echo
if [[ $failures -gt 0 ]]; then
  echo "Selhalo testů: $failures"
  exit 1
fi
echo "Všechny testy prošly."
