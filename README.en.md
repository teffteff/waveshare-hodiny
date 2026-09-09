# Waveshare Hodiny

🇨🇿 **[Česká dokumentace](README.md)**

An open-source information dashboard for the round 480 × 480 px
[Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm).
It displays time, date, weather, temperatures, additional sensor values and
precipitation radar data from the Czech Hydrometeorological Institute (CHMI).
Values can come from Open-Meteo without an account, optionally extended with
personal TMEP.cz sensors, or from Home Assistant. Appearance, data sources,
location, radar, brightness, animations and updates are configured in a web
interface without editing source code.

<p align="center">
  <a href="https://teffteff.github.io/waveshare-hodiny/">
    <img src="https://img.shields.io/badge/Install_firmware_in_your_browser-00BBD4?style=for-the-badge&amp;logo=googlechrome&amp;logoColor=white" alt="Install firmware in your browser" height="46">
  </a>
</p>

<p align="center">
  <strong>Simple USB installation without downloading files.</strong><br>
  Open the installer in desktop Chrome or Edge, connect the display and follow the guided steps.
</p>

---

<p align="center">
  <img src="screenshots/dashboard.png" alt="Waveshare Hodiny day dashboard" width="46%">
  <img src="screenshots/dashboard-analog.png" alt="Waveshare Hodiny analog clock in day mode" width="46%">
</p>

<p align="center">
  <img src="screenshots/dashboard-values.png" alt="VALUES clock face with a grid of eight values and a ninth below" width="30%">
  <img src="screenshots/dashboard-night.png" alt="Waveshare Hodiny red night dashboard" width="30%">
  <img src="screenshots/dashboard-analog-night.png" alt="Waveshare Hodiny analog clock in red night mode" width="30%">
</p>

<p align="center">
  <img src="screenshots/dashboard-radar.png" alt="CHMI precipitation radar on Waveshare Hodiny" width="30%">
</p>

The project is Czech and the firmware defaults to Czech. English can be selected
in the device web configuration; the setting is stored persistently and also
changes the system text and verbal date shown on the display.

## Features

- digital clock with Barlow, Liberation Sans, LCD DSEG and Doto fonts, an
  analog dial with a configurable tone and optional cardinal accents, or the
  VALUES face with a grid of up to eight values and a ninth below them,
- multiple date formats and an optional seconds ring,
- today's Czech name day below the date, computed on the device without network access,
- NTP time synchronization and the Czech time zone with daylight saving time,
- Open-Meteo support without an account or token,
- Home Assistant entities read through its REST API,
- two generic top values with individual names, units, precision, icons and
  smooth color scales,
- static and animated weather icons based on Meteocons,
- CHMI precipitation radar with a Czech map, cities and 1–15 frames,
- 25, 50, 100 and 200 km radar ranges plus a full-country view,
- optional automatic rotation between the clock, radar, news, forecast,
  aircraft and agenda, in an order you choose,
- a news screen fed by any RSS or Atom feed,
- an agenda screen fed by Google Calendar, merged across several calendars and
  coloured by the one each event came from,
- a forecast screen with hourly and daily Open-Meteo data, optionally with air
  quality, PM2.5 and grass pollen — and nine hours instead of six without it,
- an aircraft radar fed by the free adsb.fi API: nearby traffic coloured by
  altitude, 10 to 100 km ranges, an altitude filter, emergency squawk alerts,
  a watched flight and an aircraft detail with the flight route from adsb.lol,
- two additional values such as CO₂, VOC, particulate matter, humidity,
  pressure or battery level,
- eight independent values on the VALUES face, each with its own name, Home
  Assistant entity, unit, precision and color scale,
- personal TMEP.cz sensors as an optional extension to Open-Meteo values,
- custom units, decimal precision and smooth color scales,
- independent day and night brightness with manual or automatic switching,
- dots, line and comet seconds effects,
- password-protectable web configuration, backup import/export and diagnostics,
- initial Wi-Fi provisioning through Improv Serial on either USB-C connector,
- A/B OTA updates that preserve Wi-Fi and device configuration,
- a Home Assistant control API protected by a random secret,
- basic settings directly on the touchscreen.

## Required hardware

The firmware is designed exclusively for **Waveshare ESP32-S3-Touch-LCD-2.1**
with a 480 × 480 px display and 16 MiB flash. Its ST7701 display, CST820 touch
controller, PSRAM, pin configuration and partition table match this exact board.

Do not flash the binary to another product merely because it also contains an
ESP32-S3. A different pinout or flash layout may prevent the device from booting.

## Installation

### Browser installation

The public [GitHub Pages installer](https://teffteff.github.io/waveshare-hodiny/)
can flash a stable release from desktop Chrome or Edge over USB. The installer
supports Czech and English. On first visit it uses Czech for browser languages
`cs` and `sk`; every other browser language uses English. The flag buttons in
the header switch the language manually.

Factory installation uses all four binary parts and the exact offsets declared
by the release `manifest.json`. The standalone `.ota.bin` file is an application
OTA image and must not be used as a factory image.

### Wi-Fi provisioning

Public releases contain no preconfigured Wi-Fi credentials. After flashing,
connect either USB-C port and configure the network through Improv Serial in the
installer. The SSID and password are stored in NVS and survive a restart.

The board exposes one USB–UART connector through CH343P and one native ESP32-S3
USB connector. Production firmware handles Improv Serial on both transports.

## First start

1. Install the firmware and provision Wi-Fi through Improv Serial.
2. Wait for the device to connect; its IP address appears in the settings screen.
3. Open `http://waveshare-hodiny.local/`. Use the displayed IP address if mDNS
   is unavailable on your network.
4. Select Open-Meteo with TMEP.cz or Home Assistant and search for the device
   location.
5. For Home Assistant, enter its URL and a long-lived access token, then test
   the connection.
6. Configure the dashboard, radar and brightness and save the changes.

A clean configuration uses Open-Meteo, Brno as the location and the full Czech
Republic radar view. Home Assistant is optional.

## Data sources

### Open-Meteo

Open-Meteo is the default source and requires no account or token. It supplies
the current weather and four configurable values. The selected city coordinates
also define the center of local CHMI radar views. Each of the four slots can
independently display 0–2 decimal places; the same setting also applies when a
TMEP.cz value is selected.

### TMEP.cz as an Open-Meteo extension

Your own TMEP.cz sensors can be added to Open-Meteo mode. Paste the complete URL
from **Extended JSON – with all sensors**, select **Verify and load sensors**, and
values from up to 32 sensors are appended to the same four selectors under a
TMEP.cz group. The firmware uses the unit returned by the export, including for
custom quantities.

When at least one TMEP value is selected, the complete export is fetched with
one HTTPS request every minute. With no selected TMEP value, the catalog is
loaded once after boot and is not refreshed periodically. Opening the web
configuration displays the cached catalog first and refreshes it from TMEP.cz
at most once per page load. Open-Meteo continues to refresh independently every
10 minutes.

The firmware extracts the ID and export key and always builds the request with
`extended=1&all=1`. These credentials remain stored on the device and are never
returned by the configuration API or backup. **Remove TMEP.cz** clears the URL,
catalog, diagnostics and TMEP assignments; affected slots return to their
default Open-Meteo values.

Example export URL:
`https://tmep.cz/vystup-json.php?id=11746&export_key=XXXXXXXXsd&extended=1&all=1`

### Home Assistant

The firmware reads individual entities through the Home Assistant REST API. It
does not require MQTT, a custom integration or an administrator account.

Create a dedicated long-lived access token in the Home Assistant user profile
and use an account with only the permissions the clock requires. After saving,
the token is never returned to the browser and can only be replaced.

The current firmware permits local HTTP and HTTPS Home Assistant servers with a
self-signed or otherwise invalid certificate. Certificate validation is therefore
disabled for this Home Assistant connection only. Use it on a trusted LAN and be
aware that this does not protect the token from an active network attacker.

Suggested entities:

| Value | Example entity | Notes |
| --- | --- | --- |
| Weather | `weather.home` | Text state or supported numeric code |
| Sun | `sun.sun` | Controls automatic day/night mode |
| Left value | `sensor.outdoor_temperature` | Temperature, CO₂, PM, pressure or any numeric sensor |
| Right value | `sensor.living_room_co2` | Temperature, CO₂, PM, pressure or any numeric sensor |
| Value A/B | `sensor.living_room_co2` | CO₂, VOC, PM, humidity, pressure, etc. |

Unavailable or invalid values are displayed as `--`.

## Web configuration

<p align="center">
  <img src="screenshots/web-configuration.png" alt="Waveshare Hodiny web configuration" width="920">
</p>

The web interface configures:

- the device language; until a choice is stored, the display remains in Czech
  and the first web visit stores Czech for `cs`/`sk` browsers or English for
  every other browser language; later visits use the stored device setting and
  the fixed web header provides flag buttons for changing it at any time,
- the data source and shared geographic location,
- Home Assistant URL, token, weather and sun entities,
- left and right top values with type, name, unit, precision, icon and color
  scale,
- `Monochrome`, `Flat` and `Line` animated weather icon styles,
- CHMI radar range, map opacity, frame count, pause and automatic rotation,
- custom values, units, precision and color scales,
- the nine values of the VALUES face, each separately enabled, with its own
  entity, name, unit, precision and color scale; the section only appears with
  the Home Assistant data source, because the slots read entities,
- the order of the nine values, by dragging the handle in the header or with
  the up and down buttons; the order matches the grid on the display and the
  whole slot setting moves along, including the color scale,
- Home Assistant entity selection in every entity ID field; the arrow in the
  field opens the list, typing filters it regardless of diacritics and a
  manually typed ID stays valid. The firmware asks for the list over
  `/api/template` when a list is first opened; the Load entities button only
  refreshes it,
- clock and date colors, fonts, date format and seconds effects,
- day/night brightness and automatic switching,
- the screen order on the **Screens** tab, by dragging or with the arrows,
- automatic OTA updates and web-server availability,
- an optional web password,
- backup import/export, restart, display controls and live diagnostics.

### CHMI precipitation radar

Radar imagery comes from the open MAX_Z composite published by the Czech
Hydrometeorological Institute. Views cover 25, 50, 100 or 200 km around the
saved coordinates, or the whole Czech Republic. The map includes the national
outline and a range-specific selection of cities.

### Precipitation source

The radar has two sources, switched in the web settings.

The **CHMI** composite is sharper over Czechia and stays the default, but it
has nothing to show beyond the border. It is therefore available only when
Open-Meteo location search identifies the saved country as `CZ`. For locations
outside Czechia the firmware does not start the CHMI radar, download its data
in the background or respond to radar gestures, and automatic screen rotation
is disabled. Open-Meteo weather and Home Assistant remain available without
this restriction.

**RainViewer** covers Europe and the world, is free and needs no key, it is
just coarser. With it selected the radar works outside Czechia too, and the
map underlay switches to European country outlines and European cities; Czech
cities keep the abbreviations you are used to. RainViewer serves Web Mercator
tiles and its zoom goes in powers of two, so you get the nearest available
range rather than exactly the number you set - the caption therefore shows the
radius actually reached. Tiles are not cached, so changing the range downloads
the animation again.

### Intensity scale

An optional scale along the left edge shows six shades with reflectivity in
dBZ and the matching rainfall in mm/h, converted with the Marshall-Palmer
relation (Z = 200 R^1.6). Both palette and labels follow the selected source,
because the same yellow means 40 dBZ on one scale and 35 on the other; its own
numbers say which one you are reading. It covers part of the map and can be
switched off, which gives the space back to the city labels.

### Radar screen layout

The screen keeps fixed bands above and below the map, so the same kind of
information always sits on the same line:

1. the **screen indicator** at the very top (shared by every screen, see below),
2. the **clock and outside temperature** — you can check the time without
   switching back to the clock screen,
3. the **frame dots**, one per animation frame,
4. the **frame time** — the newest frame is labelled `NOW` and is bright green
   in day mode, older ones carry their age as `-25 min 14:10`,
5. at the bottom the **range** (`50 km`, or `ALL OF CZECHIA`) and below it the
   **range dots**. The data source is not named on screen, neither next to the
   range nor in the scale header. It is picked in the settings and changes at
   most once in the clock's life, so it would only take up room on every frame;
   the Radar tab and the diagnostics page both show which source is active.

The clock and temperature line can be switched off. The temperature comes from
whichever source is configured: with Open-Meteo from the forecast for the saved
city, with Home Assistant from the entity picked in **Outside temperature
entity** on the Radar tab. Without an entity the line shows only the time. The
temperature is rounded to whole degrees — a tenth of a degree is noise for an
outdoor reading and the two extra characters decide whether the line fits
inside the circle.

Until the first frame is ready, a status message stays in the middle of the
empty screen and the other labels are hidden.

One frame creates a static view; 2–15 frames create an animation from oldest to
newest. The pause after the newest frame is configurable from 0 to 30 seconds
and defaults to 5 seconds. The lit frame dot marks where in the animation you
are, and turns red while an empty cache is being fully prepared. New imagery is
checked in fixed five-minute slots, approximately one minute after the CHMI
publication time.

The clock **works the frame names out from its own clock**, because CHMI names
them after the slot in UTC (`pacz2gmaps3.z_max3d.20260909.1740.0.png`). It used
to download the directory listing for them, but that listing runs past 300 KB,
since the server keeps a week of five-minute frames in it, and the clock needed
one fact from it: where the list ends. What is left on the network is a single
HEAD request asking whether the newest slot has been published, stepping back up
to four slots when it has not. That saves the 300 KB per refresh, and more
importantly the time the download spent holding the network lock against every
other screen.

### Screen indicator

A row of dots sits at the top of **every** screen, one per screen taking part
in the rotation, in the order you set on the Screens tab. The filled dot is
the one you are
looking at. A disabled screen has no dot, so the row always matches what the
gesture can actually reach. With a single available screen the indicator is not
drawn at all, because one dot says nothing; it is also hidden in the settings
and during a firmware update.

The red night appearance converts the map, cities, location marker, labels and
precipitation intensity levels to shades of red. The newest timestamp then uses
the same red as the other text. Changing the appearance reuses the prepared
cache and does not download or rebuild the animation.

The web range buttons preview a view immediately. Blue marks the range currently
shown on the clock and amber marks the saved default. The preview becomes
persistent only after saving the configuration. A range selected on the device
is temporary and the saved web value is restored after a restart.

Automatic rotation is disabled by default and provides separate clock, radar
news and forecast durations; only the screens you enable take part in it, and a screen
opened by hand stays until the next gesture. The radar duration is a minimum:
an animation already in progress, including its final pause, always completes
before the clock returns. After a
restart, background cache preparation begins only after Wi-Fi is connected and
time synchronization has completed. The first automatic transition waits for
the complete animation, so playback starts immediately from the oldest frame.
With automatic rotation disabled, radar data is not downloaded in the
background and loading starts when the radar is opened manually.

### Czech name days

With the device language set to Czech, the name day for the current date is
shown below the date, for example `ADAM, EVA`. The table is built into the
firmware, so nothing is fetched: name days work without network access and
without Home Assistant. They are drawn on all three clock faces - digital,
analog and VALUES.

Names are uppercase because the `clock_czech` font carries only the uppercase
Czech accented characters. Days without a name day (1 January, 24 December and
a few others) stay blank. The English date shows no name day, since it is a
Czech custom, and hiding the date hides the name day with it - a bare name
with no date would sit on the face without context.

### RSS news

A separate screen shows the latest items from any RSS 2.0 or Atom feed. Enter
the feed address on the **News** tab of the web configuration, for example
`https://www.irozhlas.cz/rss/irozhlas`. The **Test the feed** button downloads
the address before saving and shows exactly what will appear on the display.
Server certificates are validated against the Mozilla roots built into the
firmware, so any `https://` address works.

The screen shows 3 to 6 items, 5 by default. With three to five items each
headline gets three lines, which fits a typical news headline whole. A sixth
item fits only at the cost of dropping to two headline lines, so longer
headlines are cut with an ellipsis. The publication time is shown to the
left of each headline in local time; a feed without dates is shown without
times and sorts after dated items.

Headlines are transliterated to ASCII: `Ř` becomes `R` and `ř` becomes `r`. The
mapping covers the whole Latin-1 Supplement and Latin Extended-A blocks, so
Slovak, Polish and German names come through as well, and typographic quotes and
dashes are replaced by their ASCII equivalents. This is not a font limitation —
`ClockCzechFont*.c` carry the complete Czech alphabet, lowercase included, and
the name days under the date use it. A feed can arrive in any language, though,
so everything is flattened; the agenda screen, which reads only your own server,
keeps its accents.

The feed is downloaded every 5 to 120 minutes, 10 by default, even while the news
screen is closed. Opening the screen — by gesture or by the automatic rotation —
also triggers an immediate download when the cached items are older than five
minutes; fresher items are kept as they are so that rotation passes do not hammer
the feed. After a failure the firmware retries in two minutes and keeps the last
successfully loaded items on screen; an error message appears only when the feed
has never loaded. A disabled screen is not downloaded at all and is not reachable
by the gesture.

### Calendar agenda

A separate screen shows what the calendar holds for the next few days. Times sit
in one column and event names in another, so the list reads as a table rather
than as text bent to fit a circle. Above the first line of each day is `DNES`,
`ZÍTRA` or a date such as `PÁ 11.9.`; an all-day event shows a dash instead of a
time and sits at the top of its day.

The clock and the outside temperature run along the top, the same line the
forecast and radar screens carry. Along the bottom is a legend: each calendar's
name in its own colour. That colour also tints the time of every event, so you
can tell which calendar an event came from without a label on each row. In the
red night palette the colours merge into one and only the legend distinguishes
them, because anything but red would spoil night vision.

**The server talks to Google, the clock does not.** The clock reads a finished
list from an address you enter in the **Agenda** tab, typically
`https://your-server.example.net/agenda.json`. The server reads the calendars
through a service account, merges them, expands recurring events and even builds
the day labels, so no date arithmetic is left in the firmware and no token is
stored on the clock. Which calendars appear is therefore configured on the
server; the setup and the source files live in [infra/](infra/README.md).

**The address may carry a password.** The agenda, unlike the news feed, is
private, so on the reference server it sits behind HTTP basic auth and is
entered as `https://user:password@your-server.example.net/agenda.json`. The
firmware does nothing special for that: `HTTPClient` takes the credentials out
of the address and sends them itself. The address must then be `https://`, as
on `http://` the password would travel in the clear.

Between 3 and 12 events can be shown, 8 by default. The list starts below the
header and grows downwards. Every extra day takes one line for its heading, so
with a longer horizon the last events do not fit and the screen leaves them out
— a row running over the legend would be worse than one event fewer. The **Test the
agenda** button downloads the address before you save and shows what will appear
on the display.

The agenda is downloaded every 5 to 120 minutes, 15 by default, even while the
screen is closed. The server recomputes it every quarter of an hour anyway, so
asking more often brings nothing new. An empty calendar is **not an error**: the
screen says `Nic naplánovaného` and the automatic rotation skips it, because
rotating to a page that only reports emptiness is not worth a slot — the finger
hold still reaches it whenever you want. After a failure the firmware retries in
two minutes and keeps the last successfully loaded events on screen.

### Weather forecast

A separate screen shows the Open-Meteo forecast for the city saved on the
**General** tab. It does not matter where the clock face reads its values from:
the configuration holds the coordinates even with Home Assistant selected, so
the screen works in both modes. Enable it on the **Weather** tab; it starts
disabled, so a firmware upgrade never adds it on its own.

The header carries the time and the outside temperature, just like the radar
status line — the forecast fills the whole display and the clock face below is
not visible. Under it come the hourly rows (hour, icon, temperature,
precipitation, wind) and, below a divider, the daily rows with a weekday
abbreviation and the high/low pair. Precipitation under a tenth of a millimetre
is left blank so the column is not a forest of zeros.

<p align="center">
  <img src="screenshots/forecast-air-quality.png" alt="Forecast screen with the air quality section" width="46%">
  <img src="screenshots/forecast-nine-hours.png" alt="Forecast screen without air quality, showing nine hours" width="46%">
</p>

At the bottom sits an optional section with the European air quality index, the
PM2.5 concentration and grass pollen; the values are colored by the European
Environment Agency bands. Pollen is only modelled by the European CAMS domain,
so outside Europe the row shows a dash.

**The hour count is not a setting, it is derived.** The circular display has a
fixed number of rows and everything else that takes a row takes it from the
hours. The air quality section occupies the bottom three rows: with it the
screen fits **six hours**, without it **nine**. Every day removed (0 to 4, 3 by
default) is likewise one more hour. The web hint next to the air quality switch
states how many hours the current combination yields and how many the other one
would; the numbers come from the firmware so they cannot drift from what the
screen actually draws.

The forecast is downloaded every 10 to 180 minutes, 30 by default, even while
the screen is closed. Opening the screen — by gesture or by the automatic
rotation — also triggers an immediate download when the cached data is older
than fifteen minutes. After a failure the firmware retries in two minutes and
keeps the last successfully loaded forecast on screen; an error message appears
only when the forecast has never loaded. Air quality is an extra: if it fails,
the forecast still appears, just without the bottom section. A disabled screen
is not downloaded at all and is not reachable by the gesture.

### Aircraft radar

The screen shows nearby aircraft on the same map the weather radar uses.
Positions come from the free [adsb.fi](https://opendata.adsb.fi/) API and the
route of the selected flight from [adsb.lol](https://api.adsb.lol/); both are
key-free and need no registration. The location is the same city the weather
uses, so there is nothing to configure twice.

Optionally you can put **your own feed** between the clock and adsb.fi: the
*Your own aircraft feed* field on the Aircraft tab, served by
[`infra/planes`](infra/planes/). At the 100 km range the adsb.fi response runs
past 50 kB, yet the firmware reads twelve keys out of roughly fifty; the server
drops the rest along with the traffic on the ground, and the same sample comes
down to about 13 kB. The shape of the response stays the same, so it really is
just a shorter road to the same thing. The same address also serves the route of
the selected flight, so the clock talks to one server instead of two, and the
server remembers a route for ten minutes, which makes a second tap on the same
aircraft instant. An empty field means asking adsb.fi and adsb.lol directly, and
that is how it stays across a firmware upgrade, so the screen keeps working with
no server at all.

An aircraft's colour carries its altitude — red below 2 km, orange from 2 to
6 km, yellow from 6 to 10 km and blue from 10 km up; an aircraft that reports
no altitude is grey. A scale with the band edges is drawn under the aircraft
count, so the colours need not be memorised. The icon is turned to match the
ground track, and when an aircraft broadcasts no track a circle is drawn
instead of an arrow.

Swiping across the display steps the range through 10, 25, 50 and 100 km, just
as it does on the weather radar; the dots below the readout show how many steps
are left. A range picked by touch lasts until the next restart, after which the
value saved through the web takes over again.

The settings let you choose **which compass bearing sits at the top of the
display** — that is, the direction you are looking out of the window. The whole
projection turns with it, map and cities included, so what is at the top of the
display is in front of you. The display itself is not rotated, only the
projection, so touch mapping keeps working.

The **altitude filter** keeps only the aircraft within the given band, so the
interesting one stands out in a busy sky. The aircraft count line reports how
many are visible, so it drops with the filter; how many the server actually sent
is on the diagnostics page. The filter does not touch the watched flight or
emergencies — those are looked for before it, so an altitude setting can never
hide an emergency. Aircraft that report no altitude at all pass the filter,
because there is nothing to sort them by. Aircraft without a callsign can be
hidden separately.

An **emergency squawk** — 7500 (hijack), 7600 (radio failure) or 7700 (general
emergency) — rings the aircraft in red and takes over the aircraft count line.
A **watched flight**, given by callsign or ICAO address, gets a green ring and
passes the altitude filter as well.

Tapping an aircraft opens a detail with its altitude, speed, ground track,
climb rate, type, registration and flight route. Under the type code stands the
type spelled out — *AIRBUS A-321neo* under "A21N" — because few people know the
codes by heart. It rides in the same response as the position, so nothing extra
is downloaded for it; about one aircraft in twenty has none, because the server
does not find it in its aircraft database, and then the code stands alone.
Units switch between metric and aeronautical. Another tap anywhere closes it.
The selection is keyed on the aircraft's ICAO address rather than its position
in the list: the list is rebuilt on every fetch and its order is not
guaranteed, so an index would silently repoint the panel at a different
aircraft. When an aircraft drops out of the data for a moment the panel stays
open with the last known values and admits it with a *signal lost* note; it
closes only after three fetches without it.

The route is looked up for one selected aircraft only, never for the whole
list, and the answer is cached. The aircraft's position is sent along with the
callsign so the server can judge whether the route fits where the aircraft
actually is — without that, an aircraft over Prague was shown flying Athens –
Istanbul, because callsigns are recycled between rotations. Plenty of flights
have no route at all (general aviation, military aircraft, helicopters); that
is a normal state, not an error, and the detail says *Route unknown*. It says
the same for an aircraft without a callsign, the only thing the route can be
asked about. Until the answer arrives, *Looking up route...* stands there — and
after a network error too, because the attempt is repeated with the next
aircraft fetch.

The poll interval is 5 to 120 seconds. Larger ranges add their own minimum on
top — 10 seconds from 50 km and 15 seconds from 100 km — because they return
more data and a second either way does not matter there; adsb.fi is a free API
and the firmware behaves as a polite guest. Data is only fetched while the
screen is visible or taking part in the automatic rotation — and while it is in
the rotation but currently hidden, at most once every five minutes. All the
radar needs then is to have a frame ready for its turn; opening the screen
forces a fetch of its own. After a failure the
interval is doubled and the last good frame stays on the display: an empty sky
after one failed fetch looks like the truth but is not. A disabled screen is
never fetched and is not reachable by the gesture either.

### Color scales

Each additional value supports up to ten `value → color` points. The firmware
interpolates between neighboring points, producing a smooth scale rather than
hard color thresholds. The two values use independent scales.

### Day/night mode and seconds

Day and night brightness are independent. Automatic mode uses Open-Meteo sunrise
and sunset for the selected location or a Home Assistant sun entity. Optional
offsets adjust both transitions. With automation disabled, a short tap on either
the clock or radar switches the day and night appearance.

The configuration web server defaults to **Always on**. It can instead remain
available for ten minutes after startup or activation from the device, or be
disabled completely. Use it only on a trusted network; the dashboard gear icon
indicates an active configuration server. An optional 6–20 character password
protects web settings. The **System** tab shows an unprotected state in red and
an active password in green. Only a derived hash is stored, and the password is
not included in backups.

### Diagnostics and backups

The read-only `/diagnostics` page reports firmware, CPU, flash, current display
pixel clock, current and minimum internal RAM and PSRAM, the smallest free
stack space seen in the loop and data tasks, Wi-Fi, Home Assistant,
Open-Meteo and TMEP.cz runtime state. Radar details include the selected city, GPS,
range, prepared-frame count and time span, last successful refresh, next check,
HTTP status and the file currently being processed. Exported JSON backups
contain appearance and entity IDs but intentionally omit the Home Assistant
token, TMEP.cz export URL, web password and control API secret.

## Touchscreen settings

Screens are changed by holding a finger still for about half a second. Where
the finger rests decides the direction: the left half of the display goes one
screen back, the right half one forward. The default order is clock, radar,
news, forecast, aircraft, settings and back to the clock, so the settings are
one hold in the left half away from the clock. The first five screens can be
reordered on the **Screens** tab of the web configuration; the settings stay
last in the cycle so that leaving them always ends up in the same place.
Unavailable screens are skipped but keep their place; the settings screen can
never be switched off, so a clock with no radar, news or forecast still has a
way to the web address. The same hold leaves the settings again, discarding
anything not yet stored by the Save button.

On the radar, dragging up or right zooms in and dragging down or left zooms
out; this range change remains temporary until restart. With automatic
day/night mode disabled, a double tap on any of these screens switches the
appearance. A single tap does not, because it was too easy to hit instead of
the hold that changes screens; on the aircraft radar a single tap still selects
the aircraft under the finger or closes its detail. Gestures are recognised in software from the raw touch
coordinates rather than from the CST820 gesture register, and only once the
finger lifts, so short drags across the round display are not lost and a single
gesture never fires twice. Arrow buttons move between the three settings pages;
drags are not used inside the settings menu. Available controls include
day/night brightness, automatic mode, weather icons, seconds effects,
web-server mode and OTA checks.

## Animated Meteocons

Static monochrome icons are compiled into the firmware. Public animated icons
are downloaded from GitHub Pages and cached locally. Night mode always uses the
monochrome animation style so the icons follow the red night palette.

Only assets referenced by the firmware allowlist are published. See
[`METEOCONS_ASSET_PIPELINE.md`](METEOCONS_ASSET_PIPELINE.md) for the reproducible
asset-generation process and third-party attribution.

## OTA updates

Release firmware uses two equal 6 MiB application slots. Public builds read OTA
metadata and the application image only from the trusted GitHub Pages origin.
Before activating an image, the updater verifies HTTPS, HTTP status, declared
and received size, SHA-256, ESP32-S3 chip family and inactive-partition capacity.

If validation or writing fails, the running firmware remains active. Wi-Fi and
configuration in NVS and `clockcfg` survive a normal OTA update. A factory flash
or full erase is a separate operation and may remove user data.

Version 1.6.0 contains one historical configuration migration from public
version 1.5.5. It preserves the existing data source, Home Assistant settings,
entities and appearance, adds the radar options with the full-country view and
six frames, and leaves automatic rotation disabled. Intermediate development
schemas are not maintained as separate migration steps.

Automatic updates are disabled after a clean installation. When enabled, the
firmware checks at most once per local calendar day after 04:10. Manual and
automatic updates use the same implementation and validation.

## Home Assistant control API

The web interface shows a control endpoint containing a random 128-bit secret.
It can refresh data, control the backlight and invoke other supported actions.
Treat the URL as a credential and never publish it in screenshots, logs or Git.

## Building from source

### Dependencies

The verified toolchain uses Arduino CLI, Arduino ESP32 core `3.0.7`, LVGL
`8.3.10`, PNGdec `1.0.1` and Python 3. Do not substitute board options or flash
parameters from another ESP32-S3 board.

```bash
arduino-cli core install esp32:esp32@3.0.7 --config-file arduino-cli.yaml
arduino-cli lib install lvgl@8.3.10 --config-file arduino-cli.yaml
arduino-cli lib install PNGdec@1.0.1 --config-file arduino-cli.yaml
```

### Development build

```bash
./build.sh
./upload.sh
```

`./build.sh` uses the default home credentials from `WIFI_SSID` and
`WIFI_PASSWORD`. Run `./build.sh work` to use the separate
`WIFI_WORK_SSID` and `WIFI_WORK_PASSWORD` values.

Pass a serial port explicitly when needed:

```bash
./upload.sh /dev/cu.usbmodemXXXXXXXX
```

The development build retains USB diagnostics, screenshot commands and local
development defaults. It does not install a public OTA release automatically.

### Optional local `.env`

The entire `.env` file is ignored by Git. It can supply local Wi-Fi, Home
Assistant and Firmware Hub variables used by the existing generators. Never
commit real credentials. Generated headers belong only in the ignored
`WaveshareHodiny/local/` directory.

```dotenv
WIFI_SSID=
WIFI_PASSWORD=
WIFI_WORK_SSID=
WIFI_WORK_PASSWORD=
```

### Release build

Choose a valid SemVer version and build in the separate release workflow:

```bash
./build-release.sh 1.0.0
```

A release build must contain no Wi-Fi credentials and must keep Improv Serial
available on both USB-C connectors. Publishing a release is a separate,
explicitly authorized operation.

## USB screenshots

The development firmware supports framebuffer capture over its USB diagnostic
protocol. Use the repository script with the currently verified serial port:

```bash
./capture-screenshot.sh /dev/cu.usbmodemXXXXXXXX
```

## Repository layout

- `WaveshareHodiny/` – firmware source and embedded web interface,
- `docs/` – public installer and OTA/weather assets for GitHub Pages,
- `screenshots/` and `media/` – documentation media,
- `tools/` – generators and release validation tools,
- `build.sh` – development build,
- `build-release.sh` – isolated release build and package validation.

## Troubleshooting

- If `waveshare-hodiny.local` does not open, use the IP address shown on the
  device and check whether the web server is enabled.
- If the Home Assistant test fails, verify the URL, token, network reachability
  and entity IDs.
- A persistent `--` means the value is missing, unavailable or not numeric.
- OTA installation is available only in a compatible release build and only
  after a newer compatible version has been found.
- If USB is not detected, try a data-capable cable, the other USB-C connector
  and a direct computer port without a hub.

## Security and privacy

- Public releases contain no Wi-Fi credentials.
- Home Assistant tokens are stored locally and are not returned by the API.
- Backups omit tokens, passwords and the control API secret.
- OTA uses HTTPS and verifies the application image before activation.
- The configuration web server is intended for a trusted local network.
- Do not publish control URLs, credentials, `.env` files or generated secret
  headers.

## Acknowledgements

- [Waveshare](https://www.waveshare.com/) for the hardware and documentation,
- [LVGL](https://lvgl.io/) for the embedded graphics library,
- [Meteocons](https://meteocons.com/) for weather icon artwork,
- [Open-Meteo](https://open-meteo.com/) for weather data,
- [CHMI](https://www.chmi.cz/) for open precipitation radar data,
- [Home Assistant](https://www.home-assistant.io/) for the automation platform.

I used and adapted parts of Petr's open-source
[MeteoPlaneRadar](https://github.com/petus/MeteoPlaneRadar) project from
[Chiptron.cz](https://chiptron.cz/) while implementing the radar. Thank you for
publishing the project, the practical CHMI radar-data example and the map data
that made this integration possible.

## License

The project is licensed under the [MIT License](LICENSE). Third-party components
and assets are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
