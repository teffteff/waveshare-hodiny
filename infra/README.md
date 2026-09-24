# Serverová část hodin

Hodiny samy o sobě žádný server nepotřebují, ale několik obrazovek ho používá:
zprávy si tahají RSS kanál, který na serveru generuje jazykový model, agenda
čte Google Kalendář a hodnoty chodí z Home Assistanta. Všechno jde přes HTTPS
reverzní proxy.

Tenhle adresář je **kopie toho, co na serveru opravdu běží**. Co kam na
serveru patří, říká `manifest.txt`: podle něj nasazuje `tools/deploy.sh`
a podle něj `tools/check-stack.sh --deep` ohlásí, když se server a repozitář
rozejdou. Soubory sem patří byte na byte stejné jako na serveru — jediná
schválená výjimka je `news/news.env.example`, viz níž. Když se na serveru něco
rozbije, přijde push na telefon (viz „Hlášení poruch“).

## Server

| | |
|---|---|
| Stroj | Oracle Linux 8.10, aarch64, 11 GB RAM (OCI free tier) |
| Adresa | `$CLOCK_SSH`, veřejně `$CLOCK_HOST` (No-IP) |
| SSH klíč | `$CLOCK_SSH_KEY` |

**Konkrétní adresa, uživatel a cesta ke klíči v repozitáři nejsou.** Repozitář
je veřejný a spolu s tímhle návodem by z nich byl návod na cizí stroj. Leží
v kořenovém `.env`, které `.gitignore` vynechává — stejně jako Wi-Fi heslo
nebo token do Home Assistanta:

```sh
CLOCK_HOST=tvuj-stroj.example.net
CLOCK_SSH=uzivatel@1.2.3.4
CLOCK_SSH_KEY=$HOME/cesta/ke/klici.key
```

`tools/check-stack.sh` si je odtud načte sám. Níž se používají jako proměnné,
takže se dají příkazy kopírovat po `set -a; . ./.env; set +a`.

```sh
ssh -i "$CLOCK_SSH_KEY" "$CLOCK_SSH"
```

Klíč je od 22. 9. 2026 **ed25519** (`~/.ssh/majnr-oracle-ed25519`). Původní RSA
klíč z doby založení stroje potřeboval u každého příkazu
`-o PubkeyAcceptedAlgorithms=+ssh-rsa`, protože novější OpenSSH podpisy
`ssh-rsa` (SHA-1) odmítá. Ze serveru je odebraný, takže ta volba už nikde není;
kdyby se ve starém návodu objevila, patří pryč. Stejný klíč používají
i hlídače obchodů a obce (`WATCH_SSH_KEY` v jejich `.env`).

Pro PuTTY je týž klíč převedený do `~/.ssh/majnr-oracle-ed25519.ppk`
(`puttygen ~/.ssh/majnr-oracle-ed25519 -O private -o …ppk`). V relaci PuTTY:
Connection → SSH → Auth → Credentials → *Private key file*, pak Session →
Save. Soubor je ve formátu PPK 3, který umí PuTTY od verze 0.75; pro starší
PuTTY ho vyrobí `puttygen … --ppk-param version=2`. Původní
`majnr_private_key.ppk` je ten odebraný RSA klíč a server ho odmítne.

Klíč patří do `~/.ssh`, ne do `~/Documents`: denní stahování záloh z launchd
na klíč v `~/Documents` nedosáhne (viz „Denní stahování na Macu“). Je to
**jediný klíč, kterým se na stroj dostaneš** — `authorized_keys` uživatele
`opc` nic jiného nemá a heslem se přihlásit nejde. Ztráta klíče znamená
obnovu přístupu přes OCI konzoli. Záloha klíče (správce hesel, šifrovaný
disk) proto stojí za to.

## Na čem to celé visí

Dvě věci mimo stroj, které by vypnuly všechny hodiny naráz a které se ze
serveru zkontrolovat nedají:

- **Účet OCI je Pay As You Go** (ověřeno 22. 9. 2026). Always Free instanci,
  která sedm dní po sobě vytěžuje procesor (95. percentil), síť i paměť pod
  20 %, smí Oracle zastavit jako nečinnou — a tenhle stroj s 16 % paměti
  a zátěží kolem 0,06 to kritérium splňuje. Účtu Pay As You Go se to netýká,
  Always Free zdroje na něm zůstávají zdarma. Kdyby se účet někdy vracel na
  Free Tier, tohle platí znovu. Zátěž uměle nezvyšovat.
- **Jméno u No-IP je bezplatné** a propadne, když se do 30 dní nepotvrdí.
  Potvrzuje se ručně jednou měsíčně (e-mail od No-IP). Na `$CLOCK_HOST` stojí
  všechny adresy v hodinách, Home Assistant i HSTS, takže propadlé jméno vypne
  všechno naráz. Kdyby se to mělo změnit, vlastní doména (A záznam na
  rezervovanou veřejnou IP) je levná, protože Caddyfile má místo jména
  `{{DOMAIN}}`; staré jméno může po dobu přepisování adres v hodinách zůstat
  v Caddyfile vedle nového.

## Co na něm běží

| Služba | Port | Soubory | Kopie v repu |
|---|---|---|---|
| Caddy (HTTPS proxy) | 80, 443 | `/etc/caddy/Caddyfile`, `/etc/caddy/caddy.env`, `/etc/systemd/system/caddy.service` | `caddy/` |
| Generátor zpráv | — | `/opt/news/generate.py`, `news.service` + `news.timer` | `news/` |
| Server se zprávami | 8088, jen loopback | `/opt/news/serve.py`, `locations.py`, `news-web.service`, registr poloh v `/opt/news/state/locations/` | `news/` |
| Generátor agendy | — | `/opt/agenda/generate.py`, `agenda.service` + `agenda.timer` | `agenda/` |
| Server s agendou | 8089, jen loopback | `/opt/agenda/serve.py`, `agenda-web.service` | `agenda/` |
| Přepravčí letadel | 8090, jen loopback | `/opt/planes/serve.py`, `planes-web.service` | `planes/` |
| Přepravčí blesků | 8093, jen loopback | `/opt/lightning/serve.py`, `lightning-web.service` | `lightning/` |
| Zálohy nastavení | 8092, jen loopback | `/opt/settings/serve.py`, `settings-web.service`, data v `/opt/settings/data/` | `settings/` |
| Rozvrh a úkoly | 8094, jen loopback | `/opt/school/serve.py`, `feed.py`, `school-web.service`, přihlášení v `/opt/school/school.env` | `school/` |
| Družice a noční obloha | 8095, jen loopback | `/opt/satellites/serve.py`, `sky.py`, `requirements.txt`, `.venv`, `satellites-web.service`, dráhy a efemeridy DE421 v `/var/cache/satellites/` | `satellites/` |
| Srážková předpověď | 8096, jen loopback | `/opt/rain/serve.py`, `rain-web.service` | `rain/` |
| Výstrahy ČHMÚ | 8097, jen loopback | `/opt/warnings/serve.py`, `orp.json`, `warnings-web.service` | `warnings/` |
| Upozornění na telefon | 8098, jen loopback | `/opt/alerts/serve.py`, `alerts-web.service`, téma ntfy v `/opt/alerts/alerts.env`, stav v `/opt/alerts/state/` | `alerts/` |
| Nastavení všech hodin | 8099, jen loopback | `/opt/fleet/serve.py`, `fleet-web.service`, heslo a TOTP v `/opt/fleet/fleet.env`, registrované hodiny v `/opt/fleet/state/` | `fleet/` |
| Noční záloha dat | — | `/opt/backup/backup.sh`, `backup.service` + `backup.timer`, archivy v `/opt/backup/data/`, zašifrovaná kopie do OCI Object Storage (`offsite-key.asc`, adresa v `backup.env`) | `backup/` |
| Hlášení poruch | — | `/opt/health/check.py`, `health.service` + `health.timer`, `notify-failure@.service`, `ha-update-check.sh` + `ha-update.timer` (nová verze HA), drop-in `on-failure.conf` u každé hlídané jednotky, téma ntfy v `/opt/health/health.env`, stav v `/var/lib/health/` | `health/` |
| Hlídač obchodů a obce | 8091, jen loopback | `/opt/watch`, `/opt/ou-watch` (kód), `/var/lib/watch`, `/var/lib/ou-watch` (databáze, fotky) | vlastní repozitáře `hlidac-novinek`, `hlidac-ondrejov` |
| Statistiky letů (radar) | 8100, jen loopback | `/opt/radar` (kód), `/var/lib/radar` (databáze), `radar-collector.service` + `radar-web.service`, poloha domu v `/opt/radar/radar.env`; stránka `https://$FLEET_DOMAIN/radar/` za heslem novinek | vlastní soukromý repozitář `radar` |
| Statistiky funkcí hodin | 8101, jen loopback | `/opt/hodiny-stats` (kód), `/var/lib/hodiny-stats` (databáze), `stats-collector.service` + `stats-web.service`, poloha domu a token do HA v `/opt/hodiny-stats/stats.env`; stránka `https://$FLEET_DOMAIN/hodiny/` za heslem novinek | vlastní soukromý repozitář `hodiny-stats` |
| Home Assistant | 8123 | Docker, `--network=host`, config bind-mount | — |
| Ostatní | 25565, 24454/udp | Minecraft (ruční start v `tmux` pod `opc`), go2rtc z HA — s hodinami nesouvisí | — |

Generátor jede na **Google Gemini**, ne na Claude: `.venv` s `google-genai`,
klíč `GEMINI_API_KEY` v `/opt/news/news.env` (práva 600). Timer pouští výběr
8× denně mezi 06:05 a 20:05. Když cokoli selže, skript skončí nenulově a
**nechá předchozí soubor být** — na hodinách zůstanou starší zprávy místo
prázdna. Stará stopa v repu: `news/news.env.example` je oproti serveru
opravená (server má pořád původní variantu s `ANTHROPIC_API_KEY` z doby, kdy
se čekalo, že generátor pojede na Claude). Kontrola shody tenhle soubor
schválně přeskakuje.

**Zprávy podle polohy hodin.** Adresa kanálu v hodinách může nést zástupné
značky: `https://$CLOCK_HOST/top.xml?city={city}&lat={lat}&lon={lon}`.
Firmware je nahradí městem a souřadnicemi z polohy zařízení (záložka Počasí);
adresám bez značek polohu neposílá. `serve.py` si polohu zaokrouhlenou na
desetinu stupně zapíše do registru a dokud pro ni generátor nic neudělal,
vrací společný `top.xml`. Každý běh timeru pak kromě společného výběru udělá
pro každou polohu viděnou za posledních 7 dní vlastní `top-<klíč>.xml`
s regionálními zdroji navíc (`NEWS_REGIONAL_FEEDS`, výchozí ČT24 Regiony)
— jedno volání modelu na polohu. Registr má strop `NEWS_MAX_LOCATIONS`
(výchozí 4, musí sedět v `news.env` i `news-web.service`), protože server
visí na veřejné IP; nové hodiny tak dostanou místní výběr nejpozději po
dalším běhu timeru (přes noc až 10 h). Výběr pro polohu, který zaostává
za společným o víc než 3 h, se nevydává.

**Stav k 8. 9. 2026:** vyřešeno. Store má `use_x_forwarded_for` i
`trusted_proxies`, `http:` blok je z YAML pryč, obě řádky `header_up` jsou
z Caddyfile pryč a HA v logu ukazuje skutečnou IP klienta, ne `127.0.0.1`.

**Práh je nastavený.** `login_attempts_threshold` je `6` a `ip_ban_enabled`
`true`, takže se po šesti neúspěšných pokusech banuje automaticky — a protože
HA zná pravou IP klienta, chytne se útočník, ne `127.0.0.1`. Bany se zapisují
do `ip_bans.yaml` vedle `configuration.yaml`; dokud ten soubor neexistuje,
není zabanovaný nikdo. Odbanování = smazat řádek a restartovat HA. Práh se
mění v Nastavení → Systém → Síť.

## Adresy, které používají hodiny

```
https://$CLOCK_HOST/top.xml      zprávy
https://hodiny:$PLANES_PASSWORD@$CLOCK_HOST/planes.json  letadla (nepovinné)
https://hodiny:$LIGHTNING_PASSWORD@$CLOCK_HOST/lightning.json  blesky (nepovinné)
https://hodiny:$SCHOOL_PASSWORD@$CLOCK_HOST/school.json  rozvrh a úkoly (nepovinné)
https://hodiny:$SATELLITES_PASSWORD@$CLOCK_HOST/satellites.json  družice (nepovinné)
https://hodiny:$RAIN_PASSWORD@$CLOCK_HOST/rain.json  srážková předpověď (nepovinné)
https://hodiny:$WARNINGS_PASSWORD@$CLOCK_HOST/warnings.json  výstrahy ČHMÚ (nepovinné)
https://hodiny:$ALERTS_PASSWORD@$CLOCK_HOST/alerts  upozornění na telefon (nepovinné)
https://hodiny:$SETTINGS_PASSWORD@$CLOCK_HOST/settings  zálohy nastavení (nepovinné)
https://hodiny:$AGENDA_PASSWORD@$CLOCK_HOST/agenda.json  agenda z kalendáře
https://$FLEET_HOST              vzdálená správa (nepovinné; adresa + token, viz níž)
https://$CLOCK_HOST              Home Assistant
```

Adresa zpráv a Home Assistanta se do hodin nezadává ručně: obě jsou
v kořenovém `.env` (`NEWS_URL`, `HOME_ASSISTANT_URL`),
`tools/generate_secrets.py` z nich udělá `WaveshareHodiny/local/secrets.h`
a vývojový build je předvyplní do prázdné konfigurace. Přepisuje se **jen
prázdné pole**, takže ručně zadanou adresu build nepřemaže — na zařízení,
které má v NVS ještě starou `http://` adresu, je proto potřeba ji jednou
přepsat ve webovém rozhraní.

**Agenda mezi ně schválně nepatří.** Její adresa nese heslo, a to nemá co
dělat ve zdrojáku, ze kterého se sype build. Do hodin se opíše jednou ručně
v záložce **Agenda**; v `.env` leží jako `AGENDA_URL` jen proto, aby se
nemuselo pamatovat, a `AGENDA_PASSWORD` z něj čte `tools/check-stack.sh`.

Firmware kvůli HTTPS měnit netřeba: RSS používá celý CA bundle a Let's Encrypt
se do něj řetězí. Home Assistant certifikát **neověřuje vůbec**
(`setInsecure()`), aby fungoval i místní server se self-signed certifikátem —
token je tak chráněný jen před odposlechem, ne před podvrženým serverem.

## Porty

Musí být otevřené **na dvou místech** — v OCI (security list dané VCN) i ve
`firewalld` na stroji. Když ACME hlásí „Timeout during connect (likely firewall
problem)“, jedno z těch dvou je zavřené.

- 22 — SSH, jen klíčem, hlídá ho fail2ban (viz „Zabezpečení stroje“)
- 80, 443 — Caddy a obnova certifikátu. **Bez nich certifikát tiše vyprší.**
- 25565/tcp+udp, 24454/udp — Minecraft, s hodinami nesouvisí, ale mají zůstat

Nic dalšího otevřené není (ověřeno zvenčí 13. 9. 2026). Porty **8088, 8089,
8090, 8092, 8093, 8094, 8095, 8096, 8097, 8098 a 8099 mezi ně nepatří**: servery se zprávami, agendou, letadly,
blesky, zálohami, rozvrhem, družicemi, srážkami, výstrahami, upozorněními a vzdálenou správou poslouchají jen na `127.0.0.1`, protože jinak by šlo heslo
z Caddyfile obejít dotazem přímo na ně. Otevřít ho v OCI nebo ve `firewalld` by tu ochranu zrušilo.
Home Assistant poslouchá na 8123 na všech rozhraních (`--network=host`), ale
ve `firewalld` otevřený není; ven chodí jen přes Caddy.

Certifikát vydává Let's Encrypt přes tls-alpn-01, obnovuje ho Caddy sám.
Neúspěšné pokusy jsou limitované (~5/h), takže **restartovat Caddy kvůli
opakování nemá smysl** — sám si počká.

## Agenda z Google Kalendáře

`generate.py` čte kalendáře přes **servisní účet** Google Cloudu, ne přes
uživatelský OAuth, a každých 15 minut zapíše snímek všech událostí na týden
dopředu do `/opt/agenda/www/events.json` (práva 600). Odpověď `/agenda.json`
z něj skládá `serve.py` až při dotazu (logika je ve `feed.py`), protože každé
hodiny chtějí jiný výběr kalendářů. Nese hotové řetězce: popisek dne, čas,
titulek a index kalendáře. Časová zóna, expanze opakovaných událostí
i skládání popisků se dělají tady, aby ve firmwaru nezůstala žádná datumová
aritmetika.

Adresa servisního účtu, se kterou se kalendáře sdílejí, je v jeho klíči jako
`client_email`:

```sh
ssh … "sudo grep client_email /opt/agenda/key.json"
```

Přístup nedávají role v Cloudu, ale **sdílení kalendáře s adresou účtu**
(`…@…iam.gserviceaccount.com`) ve webovém rozhraní Kalendáře, oprávnění
„Zobrazit všechny podrobnosti události". Mobilní aplikace to neumí. Není
potřeba souhlasná obrazovka ani doménová delegace.

Proč ne uživatelský OAuth: klient, který zůstane ve stavu „Testing", vydává
refresh tokeny s **platností sedm dní**. Obrazovka by každý týden zhasla.
Klíč servisního účtu nevyprší.

Dvě pasti, které stály čas:

- Kalendář, ke kterému účet nemá přístup, vrací **404 `notFound`**, ne 403.
  „Neexistuje" a „nemáš k němu právo" tedy vypadají stejně. Než začneš
  pochybovat o ID, zkontroluj seznam sdílení.
- Sdílený kalendář se **neobjeví** v `users/me/calendarList` servisního účtu;
  ten zůstane prázdný napořád. Ptát se je potřeba přímo na ID kalendáře,
  prázdný seznam není důkaz rozbitého sdílení.

Kalendáře se vypisují v `AGENDA_CALENDARS` v `/opt/agenda/agenda.env` (práva
600). **Pořadí je významné:** index kalendáře v tom seznamu si hodiny berou
jako barvu, kterou událost odliší, a pamatují si podle něj i to, které kalendáře
majitel ve webovém rozhraní schoval (`?hide=1,2` v dotazu). Nový kalendář proto
patří na konec seznamu. Za svislítkem může stát jméno do legendy, které přepíše
to z Googlu: `id|Jméno`. Klíč účtu leží v `/opt/agenda/key.json`,
práva 600, v adresáři s právy 700. Ani jeden soubor nepatří do repozitáře.

Účet i klíč vznikly v projektu `gen-lang-client-…`, tedy v tom, který Googlu
založilo AI Studio pro klíč Gemini. Je to schválně: generátor zpráv i agenda
sdílejí jeden projekt.

**Přechodné chyby Googlu se opakují.** Calendar API občas vrátí 503 nebo 429
na jediný dotaz; 22. 9. 2026 tím spadl běh a odešel zbytečný push. `get()`
v `generate.py` proto 429, 5xx, výpadek spojení i timeout zkusí znovu po 5, 15
a 30 s, dokud od startu běhu neuplyne 80 s (`AGENDA_RETRY_BUDGET`,
`TimeoutStartSec` je 120). 404 a ostatní 4xx se neopakují: znamenají odebrané
sdílení nebo chybu v konfiguraci, a ty mají přijít hned. Testy bez sítě
i bez knihoven Googlu: `python3 -m unittest infra/agenda/test_generate.py`.

Prázdná agenda je **legitimní stav** — kalendář prostě nemusí nic mít. Proto ji
`check-stack.sh` hlásí jako `warn`, ne jako `FAIL`, na rozdíl od prázdného
kanálu se zprávami, kde prázdno vždycky znamená rozbitý běh.

### Soukromé kalendáře

Kalendář v `AGENDA_PRIVATE_CALENDARS` dostanou jen hodiny, které pošlou heslo
v hlavičce `X-Agenda-Key`. Heslo k agendě v adrese na to nestačí: adresu vidí
každý, kdo otevře webové rozhraní hodin. Soukromé kalendáře dostanou indexy až
za veřejnými, takže jejich přidání nepřebarví ty stávající.

`serve.py` porovnává heslo s otiskem `AGENDA_PRIVATE_HASH` (PBKDF2-SHA256) ze
stejného `agenda.env`, který proto čte i `agenda-web.service`. Bez hlavičky se
soukromé kalendáře jen vynechají; se špatným heslem odpoví 403, aby se chyba
ukázala ve webovém rozhraní hodin. Po deseti špatných pokusech za čtvrt hodiny
neodemkne soukromé kalendáře nikomu, ani se správným heslem, dokud okno
neuplyne.

Přidání soukromého kalendáře:

1. V Google Kalendáři na webu (mobilní aplikace to neumí): **Nastavení →
   Nastavení mých kalendářů → kalendář → Sdílet s konkrétními lidmi → Přidat
   lidi** → adresa servisního účtu, oprávnění „Zobrazit všechny podrobnosti
   události". ID kalendáře je o kus níž v sekci **Integrovat kalendář**;
   u hlavního kalendáře účtu je to přímo e-mailová adresa.
2. Otisk hesla (heslo jen tisknutelné ASCII, nejvýš 63 znaků — jde v HTTP
   hlavičce):
   ```sh
   ssh -t … "sudo -u agenda /opt/agenda/.venv/bin/python /opt/agenda/serve.py --hash"
   ```
3. Do `/opt/agenda/agenda.env` přidat
   `AGENDA_PRIVATE_CALENDARS=id|Jméno` a vypsaný `AGENDA_PRIVATE_HASH=…`,
   pak `sudo systemctl restart agenda-web.service && sudo systemctl start
   agenda.service`.
4. V hodinách v záložce **Agenda** zapnout kalendář a zadat heslo.

## Heslo k agendě

Kanál se zprávami může číst kdokoli, agenda ne: nese titulky událostí
z rodinného kalendáře. Adresa serveru přitom tajná není — Let's Encrypt každý
vydaný certifikát zapisuje do Certificate Transparency, takže se jméno stroje
dá vyčíst i bez toho, aby ho někdo někde zveřejnil. Proto je `/agenda.json`
od 9. 9. 2026 za HTTP basic auth.

**Firmware se kvůli tomu neměnil.** `HTTPClient` v jádru ESP32 si uživatele
a heslo vytáhne z adresy tvaru `https://uzivatel:heslo@host/agenda.json`
a přiloží je hned k prvnímu požadavku jako hlavičku `Authorization`. Do hodin
se tedy zadává jen delší adresa. Musí být `https://` — na `http://` by heslo
šlo po drátě otevřeně, protože se posílá bez čekání na výzvu.

Kde co leží:

| | |
|---|---|
| Uživatel | `hodiny`, natvrdo v `caddy/Caddyfile` — tajemstvím je jen heslo |
| Hash hesla | `/etc/caddy/caddy.env`, práva 600, čte ho `EnvironmentFile=` v `caddy.service` |
| Heslo | kořenový `.env` jako `AGENDA_PASSWORD` a celá adresa jako `AGENDA_URL` |
| Hodiny | webové rozhraní, záložka **Agenda**, pole s adresou |

V `Caddyfile` stojí jen `{env.AGENDA_HASH}`, protože repozitář je veřejný
a bcrypt hash by z něj šel lámat offline. Kopie v repu je proto pořád byte na
byte stejná jako soubor na serveru a `tools/check-stack.sh --deep` ji umí
porovnat beze změny.

Výměna hesla:

```sh
openssl rand -hex 24          # hex schválně: bez : a @ kvůli adrese, bez $ kvůli .env
# → do .env jako AGENDA_PASSWORD a celou adresu jako AGENDA_URL
tools/deploy.sh --hash AGENDA
```

`--hash` vezme heslo z `.env`, v `/etc/caddy/caddy.env` přepíše jen řádek
`AGENDA_HASH` (soubor nese i ostatní `*_HASH` a bez nich by Caddy nenaběhl)
a Caddy **restartuje**, ne reloadne: proměnnou z `caddy.env` dostane běžící
proces jen při startu, takže po reloadu by dál platil starý hash. Nakonec se
nové heslo opíše do hodin — dokud se nezmění tam, obrazovka s agendou zůstane
prázdná a `check-stack.sh` to ohlásí.

`tools/check-stack.sh` kontroluje obojí: že bez hesla přijde 401 (jinak by
agendu četl kdokoli) a že s heslem z `.env` dorazí čerstvý JSON.

## Zálohy nastavení

Hodiny umí uložit celé nastavení na server a jiné hodiny si ho odtud stáhnou
(záložka **Systém → Záloha a sdílení nastavení**). `settings/serve.py` je jen
úložiště pojmenovaných souborů: `GET /settings/` vrátí seznam, `GET`, `PUT` a
`DELETE` na `/settings/<název>` jednu zálohu čtou, ukládají a mažou. Mazat jde
z hodin tlačítkem **Smazat ze serveru**, nebo `rm /opt/settings/data/<název>.json`.

**Záloha nese token Home Assistantu a hash hesla webu**, proto:

- stojí za `basic_auth` v Caddy s **vlastním heslem** (`SETTINGS_HASH`), ne
  s tím k agendě – to zná každé hodiny, které agendu jen čtou,
- server poslouchá jen na `127.0.0.1`, port 8092 se nikde neotevírá,
- název je `[a-z0-9][a-z0-9-]{0,31}`, takže z něj nejde složit cesta,
- soubory mají práva 600 v adresáři 700, zápis jde přes dočasný soubor
  a `os.replace`,
- tělo má strop 64 kB (Caddy i server) a musí to být obálka zálohy hodin,
- záloh je nejvýš 64, aby se disk nedal zaplnit ani s heslem.

Každá záloha je obálka verze 4 zašifrovaná heslem zálohy (AES-256-GCM, klíč
z PBKDF2-SHA256 ve firmwaru), které server nikdy nevidí; čitelný zůstává jen
autentizovaný popis pro seznam. Server obsah neotevírá, kontroluje jen tvar
obálky a meze iterací (10 000 až 100 000). Starší nešifrované obálky odmítne
(400) a do seznamu je nezařadí.

Hodiny bez hesla webu zálohu nevytvoří. Firmware přijme jen adresu `https://` a přesměrování nenásleduje, aby
heslo z adresy nešlo jinam.

Zavedení (jednou):

```sh
openssl rand -hex 24               # do .env jako SETTINGS_PASSWORD
tools/deploy.sh --hash SETTINGS    # SETTINGS_HASH do caddy.env, restart Caddy
tools/deploy.sh --init settings    # uživatel, /opt/settings 750, data/ 700, jednotka
tools/deploy.sh caddy              # Caddyfile s blokem /settings/*
```

**Pořadí je důležité: `--hash` před Caddyfilem.** `{env.…}` čte běžící proces
Caddy a proměnné z `caddy.env` dostane jen při startu. Caddyfile s novým
blokem by bez restartu spadl na prázdném `SETTINGS_HASH` (stalo se při
nasazení 13. 9. 2026); `deploy.sh` v takovém případě vrátí předchozí Caddyfile
a napíše proč. Do hodin se pak zadá adresa
`https://hodiny:$SETTINGS_PASSWORD@$CLOCK_HOST/settings`.

Výměna hesla: nové `SETTINGS_PASSWORD` v `.env` a `tools/deploy.sh --hash
SETTINGS`. Hodiny si novou adresu uloží po prvním úspěšném spojení.

## Blesky

Realtime blesky ze sítě Blitzortung posílá LightningMaps.org přes WebSocket
(`wss://live2.lightningmaps.org/`). Pro hodiny je to nevhodný tvar, a proto
trvalé spojení drží `lightning/serve.py` — **jedno pro všechny hodiny**:

- spojení je trvalé a šifrované; na hodinách by drželo dva 16kB TLS buffery,
- výřez, o který si klient řekne, server bere jen jako vodítko: při měření
  13. 9. 2026 dorazilo z požadovaného obdélníku jen **23 %** úderů, zbytek
  z půl Evropy, přes kilobajt za sekundu,
- protokol není zdokumentovaný (výzva `k`, úvodní dávka přes 50 kB); když se
  změní, oprava je tady a ne v novém firmwaru ve všech hodinách.

Server se k LightningMaps připojí **až na první dotaz** a odpojí se, když se
hodiny čtvrt hodiny neozvou. Výřez skládá ze všech kruhů, na které se hodiny
ptaly posledních deset minut, a když přibude nový, rozšíří ho tímtéž spojením.
Údery drží půl hodiny v paměti.

Hodiny se ptají `GET /lightning.json?lat=…&lon=…&r=…&since=…`. `r` je poloměr
v km (nejvýš 400) a pokrývá okolí polohy pro výstrahu i pohled meteoradaru.
`since` je `time` z předchozí odpovědi: server vrátí jen údery, které **přijal**
později. Počítá se podle času přijetí, ne úderu, protože LightningMaps některé
údery doručuje až minuty pozdě (naměřeno 223 s) a podle času úderu by se
ztratily. Odpověď má tvar zprávy LightningMaps
(`{"time":…,"live":true,"strokes":[{"time":ms,"lat":…,"lon":…,"id":…}]}`);
`live: false` znamená, že spojení výřez hodin ještě nepokrývá, takže prázdný
seznam neznamená „neblýská se“. Hodiny se pak zeptají znovu za pět sekund.

Stav spojení ukáže `curl 127.0.0.1:8093/lightning/status` přímo na serveru
(Caddy tuhle cestu ven nepouští).

**Data patří přispěvatelům Blitzortung.org** a podle hlavičky jejich souborů
se nesmí dál zveřejňovat ani komerčně využívat. Heslo tu proto na rozdíl od
letadel chrání obsah: `/lightning.json` je soukromý zdroj pro vlastní hodiny.

| | |
|---|---|
| Uživatel | `hodiny`, natvrdo v `caddy/Caddyfile` |
| Hash hesla | `/etc/caddy/caddy.env` jako `LIGHTNING_HASH` |
| Heslo | kořenový `.env` jako `LIGHTNING_PASSWORD` a celá adresa jako `LIGHTNING_URL` |
| Hodiny | záložka **Meteoradar**, sekce **Blesky** |

Zavedení (jednou), stejně jako u ostatních služeb:

```sh
openssl rand -hex 24               # do .env jako LIGHTNING_PASSWORD (+ LIGHTNING_URL)
tools/deploy.sh --hash LIGHTNING
tools/deploy.sh --init lightning
tools/deploy.sh caddy
```

Nasazeno 14. 9. 2026;
`tools/check-stack.sh` hlídá heslo, živé spojení i shodu souborů. Testy bez sítě:
`python3 -m unittest infra/lightning/test_serve.py`.

## Letadla

Radar letadel se ptá veřejného API adsb.fi a jeho odpověď je při dosahu 100 km
přes **50 kB**, z toho ale firmware čte dvanáct klíčů ze zhruba padesáti.
`planes/serve.py` proto stojí mezi: stáhne totéž, zahodí letadla na zemi
i klíče, které nikdo nečte, seřadí zbytek od nejbližšího a nechá nejvýš sto
padesát — tolik, kolik jich firmware stejně udrží. Ze stejného vzorku zbude
kolem **13 kB**.

Tvar odpovědi zůstává **schválně stejný jako u adsb.fi** (`{"ac":[…]}`), takže
firmware nepotřebuje druhý parser a přepnutí zpátky na přímý zdroj je otázka
vymazání adresy v nastavení. Hodiny posílají `lat`, `lon` a `dist` jako
parametry dotazu; `dist` je v **námořních mílích**, stejně jako u adsb.fi.

Táž adresa obstará i **trasu** vybraného letu: s parametrem `route=CSA1234`
místo `dist` se místo seznamu letadel vrátí trasa z api.adsb.lol, zase jen
s klíči, které firmware čte (548 B se scvrkne na 197 B). Hodiny tak mluví
s jediným jménem a jediným certifikátem místo dvou. Trasa se během letu nemění,
takže se drží deset minut a druhé klepnutí na totéž letadlo se k api.adsb.lol
vůbec nedostane. Pamatuje se i neúspěch, na dvě minuty: to API na některé
volací značky odpovídá chybou 500 opakovaně a hodiny to zkoušejí třikrát.

Na rozdíl od zpráv a agendy tu není generátor ani timer: letadla se hýbou,
takže se nedá nic připravit dopředu. Server je přepravčí, který stahuje **jen
když se někdo zeptá**, a odpověď pár sekund drží v paměti. Víc hodin v jedné
domácnosti tak sdílí jedno stažení a adsb.fi dostane dotazů míň, ne víc.
Po neúspěchu se ještě minutu půjčuje ta poslední povedená.

V hodinách je pole **Vlastní zdroj letadel** na záložce Letadla. Prázdné
znamená ptát se adsb.fi přímo, takže obrazovka funguje i bez serveru — a po
povýšení firmwaru zůstane prázdné, aby se radar sám od sebe nepřesměroval.

### Celá data pro statistiky

`/planes.json?…&full=1` vrátí odpověď adsb.fi celou — i letadla na zemi, bez
stropu 150 a se všemi klíči (zatáčení, zvolená výška, kategorie, vítr). Čte ji
sběrač statistik z repozitáře `radar` po loopbacku. Obě varianty se dělají
z téhož stažení v cache, takže adsb.fi nevidí dotaz navíc.

### Heslo k letadlům

Od 13. 9. 2026 je `/planes.json` za `basic_auth` s **vlastním heslem**
(`PLANES_HASH`). Neschovává obsah — jsou to veřejná data — ale stroj. Server
stahuje z adsb.fi a api.adsb.lol pro **libovolnou** polohu a volací značku,
pod naší IP a s User-Agentem, který odkazuje na repozitář. Cache klíčuje podle
polohy, takže kdo se ptá na mnoho různých míst, projde pokaždé až k adsb.fi
a vyčerpá jeho limit na IP. Radar na hodinách pak zůstane prázdný. Jméno stroje
je přitom veřejné (Certificate Transparency, viz heslo k agendě).

Heslo je jiné než u agendy, protože adresa letadel se opisuje do víc hodin
a neměla by stačit na čtení kalendáře. **Firmware se neměnil**: `HTTPClient`
si heslo vezme z adresy stejně jako u agendy a platí i pro dotaz na trasu,
který jde na tutéž adresu.

| | |
|---|---|
| Uživatel | `hodiny`, natvrdo v `caddy/Caddyfile` |
| Hash hesla | `/etc/caddy/caddy.env` jako `PLANES_HASH` |
| Heslo | kořenový `.env` jako `PLANES_PASSWORD` a celá adresa jako `PLANES_URL` |
| Hodiny | záložka **Letadla**, pole **Vlastní zdroj letadel** |

Zavedení (jednou). **Pořadí je důležité:** hodiny dostanou adresu s heslem
dřív, než ho Caddy začne chtít. Caddy bez `basic_auth` hlavičku `Authorization`
ignoruje, takže radar mezitím běží dál; opačné pořadí by ho na tu dobu vypnulo.

1. Heslo do kořenového `.env` (`PLANES_PASSWORD`, `PLANES_URL`) a adresu
   `https://hodiny:$PLANES_PASSWORD@$CLOCK_HOST/planes.json` do **všech** hodin,
   které vlastní zdroj letadel používají.
2. `tools/deploy.sh --hash PLANES` — hash do `caddy.env` a restart Caddy,
   ještě se starým Caddyfile.
3. `tools/deploy.sh caddy`.
4. `tools/check-stack.sh --deep`: bez hesla 401, s heslem seznam letadel.

Výměna hesla: nové `PLANES_PASSWORD` v `.env`, `tools/deploy.sh --hash PLANES`
a nová adresa do hodin.

## Družice

Obrazovka **Družice** ukazuje oblohu nad hodinami. Hodiny se ptají
`GET /satellites.json?lat=…&lon=…&groups=…&minel=…` a `satellites/serve.py`
jim vrátí pro každou družici nad obzorem azimut a výšku v desetinách stupně po
15 s na 3 minuty dopředu (`p`), výšku dráhy, vzdálenost, jestli je na Slunci,
výšku Slunce u pozorovatele a v `passes` nejbližší přelet nad 10° u ISS
a SATGUS. Hodiny mezi body interpolují a ptají se jednou za minutu.

**Přelety.** V `passes` je pro každou zapnutou skupinu s hlídanou družicí jeden
záznam: `rise`, `set`, `maxTime`, `max`, `vis` (družice na Slunci a pozorovatel
ve tmě) a u SATGUSu `pref`, podle kterého hodiny na jediný řádek pod oblohou
vyberou radši ji. Tentýž záznam ISS jde navíc ve starém klíči `pass`, protože
starší firmware seznam nezná a popisek u něj má napevno ISS. Přelet se
počítá po deseti sekundách na 36 hodin dopředu a drží se v paměti do svého
konce.

**Odkud jsou dráhy.** Z [CelesTraku](https://celestrak.org/NORAD/elements/),
skupiny `stations`, `visual`, `weather`, `gnss`, `amateur` a `starlink`, ve
formátu OMM (JSON). Skupina `satgus` je jediná družice: SATGUS (NORAD 62713,
2025-009DJ) v žádné skupině CelesTraku není, takže se stahuje dotazem
`CATNR=62713` a jinak se chová jako ostatní skupiny (stejná obnova, cache
i záloha na disku). TLE se nepoužívá: katalogová čísla nad 99999 se do něj
nevejdou. Poloha se počítá knihovnou `sgp4` (referenční implementace SGP4) pro
celou skupinu naráz přes NumPy; převod TEME → souřadnice pozorovatele je ve
`serve.py` a proti Skyfieldu sedí na setiny stupně (ověřeno 15. 9. 2026).
Deset tisíc družic Starlinku trvá kolem 50 ms.

**Kolik dotazů jde na CelesTrak.** Skupina se stahuje, jen když si o ni řekly
nějaké hodiny, a pak nejvýš jednou za 6 hodin; o kterou se dva dny nikdo
neřekl, ta se přestane obnovovat. CelesTrak data přepočítává po dvou hodinách
a IP adresy, které se ptají častěji, blokuje. Po chybě se čeká 10, 20, 40…
minut (nejvýš 6 h), na 403/429 12 h a na hlášku „has not updated“ se
pokračuje se staršími daty. Stažené skupiny leží v `/var/cache/satellites/`,
takže restart služby CelesTrak nezatíží. Dráhy starší než týden server
nevydá (u nízkých družic by ležely stovky kilometrů vedle) a odpoví 503.

**Stav** ukáže přímo na serveru `curl -s 127.0.0.1:8095/satellites/status`:
počet objektů ve skupině, stáří, jestli je chtěná, za jak dlouho další pokus
a poslední chyba. Caddy tuhle cestu ven nepouští.

**Čerstvá skupina, která ještě není stažená**, vrátí 503 s `Retry-After: 30`
(nebo 200 s `"pending"`, když jiná skupina už data má); hodiny to zkusí za půl
minuty. Odpovědi se drží 15 s, takže víc hodin v domácnosti sdílí jeden výpočet.

**Heslo k `/satellites.json`** je vlastní (`SATELLITES_HASH` v `caddy.env`).
Data jsou veřejná, ale výpočet stojí procesor a dotaz nese polohu hodin.
Firmware adresu s heslem přes `http://` neuloží.

Zavedení (jednou):

```sh
openssl rand -hex 24               # do .env jako SATELLITES_PASSWORD
tools/deploy.sh --hash SATELLITES
tools/deploy.sh --init satellites  # i .venv z requirements.txt (/usr/bin/python3.11)
tools/deploy.sh caddy
```

Aktualizace je `tools/deploy.sh satellites`: nahraje změněné soubory, doinstaluje
`requirements.txt` a službu restartuje. Novou skupinu (naposledy
`satgus`) nasaď **dřív, než se flashnou hodiny**: starší server odpoví na
neznámé jméno skupiny 400 a obrazovka družic na takových hodinách zůstane
prázdná, dokud se nasazení nedokončí.

Do hodin se pak opíše
`https://hodiny:$SATELLITES_PASSWORD@$CLOCK_HOST/satellites.json` v záložce
**Družice**. Testy bez sítě běží v repozitáři s libovolným prostředím, které má
`sgp4` a `numpy` (stejné verze jako `requirements.txt`):
`python -m unittest infra/satellites/test_serve.py`.

### Noční obloha

Druhá stránka obrazovky družic (tažení prstu) ukazuje planety, Měsíc a jasné
hvězdy v tomtéž kruhu. Počítá ji `satellites/sky.py` ve stejné službě: hodiny
se ptají **téže adresy se stejným heslem**, jen s `view=sky`, takže v hodinách
žádná další adresa není a Caddyfile se nemění.

```sh
curl -s '127.0.0.1:8095/satellites.json?view=sky&lat=49.90461&lon=14.7842&lang=cs'
{"v":1,"time":1790020800,
 "bodies":[{"id":"moon","n":"Měsíc","k":1,"ra":20.2727,"dec":-23.127,
            "dist":389012,"ill":0.763,"con":"Kozoroh","set":1790033220},...],
 "stars":[6752,-1672,-15,...],"lines":[25,26,...],
 "events":[{"t":1790789608,"tm":1,"k":"conj","x":"Měsíc 0,2° od Plejád"},...],
 "kp":2.33,"kpMax":4.0,"kpMaxAt":1790046000}
```

**Polohy jdou jako rektascenze a deklinace**, ne jako azimut a výška: hodiny si
z nich polohu na obloze dopočítají každou vteřinu z hvězdného času (proti
Skyfieldu na desetiny stupně), takže se ptají jen po 10 až 15 minutách. Měsíc
je topocentrický — jeho paralaxa je až stupeň. Planety, Slunce a Měsíc počítá
[Skyfield](https://rhodesmill.org/skyfield/) z efemerid JPL **DE421** (17 MB,
platí do roku 2053); ty si při prvním startu stáhne sám do
`/var/cache/satellites/` a do té doby obloha odpovídá 503. Hvězdy (96 nejjasnějších
a 55 čar obrazců souhvězdí) jsou tabulka v `sky.py`.

**Úkazy** (`events`) se počítají pro polohu hodin a drží se 6 h. Vybírají se
tři: nejbližší přiblížení Měsíce k planetě nebo jasné hvězdě (do 4°, resp. 2°,
na 14 dní), nejbližší roj (maximum z délky Slunce podle IMO — Kvadrantidy,
Lyridy, Perseidy, Drakonidy, Orionidy, Leonidy, Geminidy, Ursidy — i s tím, jak
moc bude svítit Měsíc), nejbližší opozice Marsu, Jupiteru či Saturnu, největší
elongace Merkuru či Venuše nebo přiblížení dvou planet (na 60 dní) a nejbližší
**zatmění viditelné z místa hodin** (na 800 dní). Zatmění Slunce se nehledá
v tabulce: kolem každého novu se po minutě porovná vzdálenost středů kotoučů se
součtem jejich poloměrů tak, jak je vidí pozorovatel, jen když je Slunce nad
obzorem, a z toho vyjde zakrytí v procentech. Zatmění má v seznamu jisté místo,
i když je daleko.

**Index Kp** je z NOAA SWPC: odhad po minutě (`kp`, jen když je mladší než
hodina) a nejvyšší předpověď na 24 h (`kpMax`). Stahuje se nejvýš po 10
minutách a **na pozadí** — NOAA odpovídá i čtyři sekundy a hodiny čekají osm,
takže dotaz dostane poslední známou hodnotu a nová přijde s dalším. Poplach na
polární záři vyhlašují hodiny podle vlastního prahu (výchozí Kp 6) a jen za tmy.

Stav oblohy je v `curl -s 127.0.0.1:8095/satellites/status` pod klíčem `sky`.
Zavedení na běžící server je aktualizace družic: `tools/deploy.sh satellites`.

Testy: `python -m unittest infra/satellites/test_sky.py`; výpočty nad efemeridami
běží jen s `SKY_EPHEMERIS_DIR` ukazujícím na adresář s `de421.bsp`, ostatní bez
sítě i bez souboru.

## Srážky

Volba **přepnout na radar, když se blíží déšť** potřebuje vědět, jestli bude
pršet — a to už někdo počítá. ČHMÚ vedle aktuální kompozice publikuje i vlastní
extrapolaci radaru:

```
composite/fct_maxz/png/pacz2gmaps3.fct_z_max.YYYYMMDD.HHMM.ft60s10.tar
```

tedy `tar` se šesti snímky na **+10 až +60 minut**, nový každých pět minut.
Snímky mají přesně tutéž geometrii i paletu jako kompozice `maxz`, kterou
firmware už dnes dekóduje: 680 × 460, osmibitová paleta, stejná projekce.

Hodiny by to zvládly samy, jenže draho: 235 kB taru každých pět minut, šest
dekódovaných PNG navíc v PSRAM a korelace, která by musela uprostřed uvolňovat
watchdog. `rain/serve.py` proto stáhne tar jednou za všechny hodiny
v domácnosti, přečte z každého snímku okolí jejich polohy a pošle dál dvě
stovky bajtů:

```sh
curl -s '127.0.0.1:8096/rain.json?lat=49.90461&lon=14.7842&r=5'
{"time":1789916045,"slot":1789915800,"covered":true,"step":10,
 "now":24,"steps":[20,24,12,12,24,24]}
```

`now` a `steps` jsou odrazivost v dBZ; `0` znamená „žádný odraz“, `-1` „snímek
chybí“. `slot` je čas analýzy, ze které předpověď vyšla, takže hodiny poznají
stará data. **Rozhodnutí „přepnout obrazovku“ tady nepadne** — práh, dohled
i prodlevu si firmware bere z vlastního nastavení, stejně jako u blesků, kde
server vozí údery a poplach vyhlašují hodiny.

**Paleta je sama stupnice.** Index 182 je pásmo 56–60 dBZ a každý další index
o čtyři dBZ níž, až po 195 = 4–8 dBZ; index 0 je bez odrazu. Ověřeno proti
`scl/scl-dbz-mmh.png`, kterou ČHMÚ publikuje vedle dat. Podél horního a pravého
okraje snímku leží svislé řezy („CZRAD – Z: MAX“), ne mapa; server si je maskuje
stejnými konstantami jako `ChmiRadarService.cpp` (`LON_DATA_RIGHT`,
`LAT_DATA_TOP`).

**Poloměr `r`** (1 až 30 km, výchozí 5) rozhoduje víc, než se čeká: nad jednou
polohou dalo 20. 9. 2026 `r=1` dvanáct dBZ a `r=30` třicet dva. Menší okolí
odpovídá na „prší na mě“, větší varuje dřív, ale častěji zbytečně. Bere se
maximum přes čtverec, aby jediný šumivý pixel nespustil poplach.

**`covered: false`** znamená, že poloha leží mimo dosah českých radarů (250 km
od Brd nebo Skalek) nebo úplně mimo snímek. Prázdno tam není sucho, jen slepé
místo — hodiny z něj nesmí udělat závěr „neprší“.

**Žádný venv.** PNG se dekóduje `zlib`em ze standardní knihovny a tar `tarfile`em,
takže služba nepotřebuje ani NumPy, ani Pillow — stejně jako letadla. Stažení
a dekódování sedmi snímků trvalo 0,6 s.

**Kolik dotazů jde na ČHMÚ.** Nic se nestahuje dopředu: když se žádné hodiny
neptají, server mlčí. Odpovědi se drží 4 minuty (krok publikace je 5), takže víc
hodin v domácnosti sdílí jedno stažení; po chybě se poslední snímky půjčují
ještě 20 minut. Nejnovější slot se hledá čtyři sloty zpět, protože tar bývá
hotový tři až čtyři minuty po čase analýzy — stejně jako `NEWEST_SLOT_PROBES`
ve firmwaru.

**Stav** ukáže přímo na serveru `curl -s 127.0.0.1:8096/rain/status`: slot,
stáří, které předstihy jsou načtené a kolik pokusů za sebou selhalo. Caddy tuhle
cestu ven nepouští.

Zavedení (jednou):

```sh
openssl rand -hex 24               # do .env jako RAIN_PASSWORD
tools/deploy.sh --hash RAIN
tools/deploy.sh --init rain
tools/deploy.sh caddy
```

Obsah (pokrytí a stáří předpovědi) hlídá `tools/check-stack.sh`. Testy bez sítě:
`python3 -m unittest infra/rain/test_serve.py`.

## Výstrahy ČHMÚ

Na meteoradaru se nad rozsahem ukáže barevný řádek s nejvážnější výstrahou
pro obec s rozšířenou působností (ORP), ve které hodiny stojí, a u vážných
výstrah se hodiny na radar samy přepnou. ČHMÚ výstrahy publikuje jako CAP XML:

```
https://opendata.chmi.cz/meteorology/weather/alerts/cap/alert_cap_50_DDHHMM.xml
```

Každý soubor je **úplný stav**, ne změna — nese všechny jevy včetně „žádná
výstraha“ a k nim seznam ORP jako kódy CISORP. Nový vychází při každé
aktualizaci (za klidného počasí jednou denně) a má 1,5 až 2,5 MB; jméno nese
jen den v měsíci a čas v UTC, takže nejnovější se pozná podle data změny ve
výpisu adresáře, ne podle jména. Soubory `_70_` jsou týdenní přehled a výstrahy
nenesou.

`warnings/serve.py` soubor stáhne jednou za všechny hodiny (nejvýš po pěti
minutách, a jen když se někdo ptá), polohu převede na ORP a pošle jen to, co se
jí týká, seřazené od nejvážnější:

```sh
curl -s '127.0.0.1:8097/warnings.json?lat=49.90461&lon=14.7842&lang=cs'
{"v":1,"time":1782680082,"sent":1782680022,"covered":true,"orp":"2122",
 "area":"Říčany","stale":false,
 "warnings":[{"id":"SIVS I.3","lvl":4,"type":5,"ev":"Extrémně vysoké teploty",
              "on":1782679895,"ex":1782684000},...]}
```

`lvl` je `awareness_level` z CAP (2 žlutá, 3 oranžová, 4 červená), `type`
`awareness_type` (1 vítr, 3 bouřky, 5 horko, 6 mráz …), `id` kód jevu ve
výstražném systému — stejný při každé aktualizaci téže výstrahy, takže podle
něj hodiny poznají novou výstrahu od prodloužené. `ex = 0` je „do odvolání“
(smogové situace). Vynechá se „žádná výstraha“ (`Minor`) i výhled (`OUTLOOK`).
Anglický text (`lang=en`) je z dvojčete bloku v `en-GB`, které CAP nese pro
Meteoalarm. Text je očištěný na znaky, které písmo hodin umí.

**Poloha → ORP.** CAP polygony nenese (dokumentace ČHMÚ to říká výslovně), jen
kódy CISORP. Hranice leží v `warnings/orp.json` (206 ORP, zjednodušené na
zhruba 200 m, 450 kB) a server v nich hledá paprskovým testem; bod do 3 km za
hranicí dostane nejbližší ORP, dál je to cizina (`covered: false`, prázdný
seznam tam neznamená klid). Soubor vyrábí `tools/build_warning_areas.py`
z hranic ORP v RÚIAN (ČÚZK) — RÚIAN má ale vlastní kódy ORP (Praha 19, ne
1100), takže se páruje podle jména: kódy CISORP ke jménům skript vyčte z archivu
CAP souborů, kde oblast „Kraj (ORP, ORP, …)“ uvádí jména ve stejném pořadí jako
kódy. Ověřeno 21. 9. 2026 pro všech 206 ORP; znovu ho stačí pustit, až se
hranice ORP změní.

**Rozhodnutí o přepnutí padá v hodinách**, stejně jako u srážek: řádek od
zvoleného stupně, přepnutí jednou pro každou novou výstrahu od zvoleného stupně
(výchozí oranžová), nejdřív hodinu před jejím začátkem.

**Stav** ukáže `curl -s 127.0.0.1:8097/warnings/status` (soubor, čas vydání,
počet výstrah, chyby). Po chybě stažení se posílá poslední stav s
`"stale":true` až 12 hodin; bez jakýchkoli dat server odpoví 502. **Žádný venv**
— XML čte `xml.etree` ze standardní knihovny.

Zavedení (jednou):

```sh
openssl rand -hex 24               # do .env jako WARNINGS_PASSWORD
tools/deploy.sh --hash WARNINGS
tools/deploy.sh --init warnings
tools/deploy.sh caddy
```

Do hodin se pak opíše
`https://hodiny:$WARNINGS_PASSWORD@$CLOCK_HOST/warnings.json` v záložce
**Obrazovky → Výstrahy ČHMÚ**. Testy bez sítě (z kořene repozitáře — spuštěné
z `infra/` by adresář `warnings` zastínil stejnojmenný modul Pythonu):
`python3 -m unittest infra/warnings/test_serve.py`.

## Upozornění na telefon

Push na telefon, když se blíží déšť nebo kolem letí něco neobvyklého:
vojenské letadlo, vzácný typ, nebo letadlo, které projde nízko nad domem.
**Hlídá server, ne hodiny**, takže upozornění chodí i se všemi hodinami
vypnutými. Hodiny jen po uložení nastavení (záložka **Obrazovky → Upozornění na
telefon**) pošlou serveru polohu a co hlídat:

```
PUT /alerts/config/<MAC bez dvojteček>   JSON, viz parse_config() v alerts/serve.py
```

Odpověď nese nadmořskou výšku polohy (`{"elevation":478.0}`). Hodiny podle ní
na obrazovce letadel označí azurovým kroužkem nízký přelet se stejnými mezemi
jako push; vojenská letadla mají purpurový kroužek podle `dbFlags` vždycky.

a opakují to po každém startu a po chybě, dokud server nepotvrdí. Vypnutá
upozornění se posílají taky (`"enabled":false`), aby server přestal hlídat;
proto se nejdřív vypíná přepínač a adresa se maže až potom.

- **Déšť** bere `alerts/serve.py` z `rain-web` po loopbacku (bez Caddy, bez
  hesla) každé dvě a půl minuty. Push odejde, když teď neprší a některý krok
  předpovědi do zvolené doby dosáhne prahu; další až po 30 minutách sucha.
- **Letadla** bere z `planes-web` po loopbacku každých 10 s, takže adsb.fi
  pořád vidí jediného volajícího s jeho cache — desetina limitu (1 dotaz/s)
  i se všemi hodinami. `planes/serve.py` kvůli tomu propouští `dbFlags` (bit 1
  vojenské, bit 2 zajímavé podle databáze tar1090) a `alt_geom`; firmware je
  nečte. Vzácnost typů se server učí sám do `state/types.json` (typ viděný
  za 30 dní v méně než třech dnech); prvních 14 dní od prvního spuštění
  vzácné typy nehlásí. Nízký přelet: přímka z polohy, kurzu, rychlosti
  a stoupání na tři minuty dopředu; výška nad zemí z `alt_geom` minus geoid
  (45 m) minus nadmořská výška polohy, kterou server jednou stáhne
  z api.open-meteo.com do `state/elevations.json`. Bez ní se nízké přelety
  nehlásí.
- Stejné letadlo znovu nejdřív za hodinu (nízký přelet) nebo za šest hodin
  (vojenské, vzácné). Hodiny na téže poloze (zaokrouhleno na ~1 km) sdílejí
  stažení i to, že push odejde jednou.

Push jde na ntfy JSONem na `NTFY_URL`, stejně jako u hlídání obchodů
(`/opt/watch`). **Téma je heslo** — vlastní, ne to od hlídání obchodů, aby se
letadla dala v aplikaci ztlumit zvlášť. **Stav**:
`curl -s 127.0.0.1:8098/alerts/status` (hodiny, stav deště, počet známých
typů, poslední chyba); ven nevede.

Každý push na letadlo se zapíše jako řádek do `state/events.jsonl`: stav
letadla, u nízkého přeletu i předpověď (vzdálenost, výška, za kolik sekund)
a nastavený práh. `curl -s '127.0.0.1:8098/alerts/events?since=0'` vrací
posledních 500; čte je sběrač statistik (soukromý repozitář `radar`), který
z nahrané dráhy pozná, jestli přelet opravdu nastal. Ven taky nevede.
Stejně se zapisuje i push na déšť (`"kinds":["rain"]`, bez `hex`): poloha,
nastavení, za kolik minut a jaká odrazivost a celá předpověď, ze které vznikl.
Zapíše se jednou i push, který podržel noční klid (`"held":true`). Ty čte
sběrač z repozitáře `hodiny-stats` a porovnává je se srážkoměrem doma.

Zavedení (jednou):

```sh
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" "$@"; }
openssl rand -hex 24               # do .env jako ALERTS_PASSWORD
tools/deploy.sh --hash ALERTS
tools/deploy.sh --init alerts      # uživatel, /opt/alerts 750, state/ 700
TOPIC="hodiny-$(openssl rand -hex 16)"   # do aplikace ntfy: Subscribe to topic
SSH "$CLOCK_SSH" "printf 'NTFY_URL=https://ntfy.sh\nNTFY_TOPIC=$TOPIC\nNTFY_TOKEN=\n' \
    | sudo install -o alerts -g alerts -m 600 /dev/stdin /opt/alerts/alerts.env \
    && sudo systemctl restart alerts-web.service"
tools/deploy.sh caddy
SSH "$CLOCK_SSH" 'sudo -u alerts env $(sudo cat /opt/alerts/alerts.env) python3.11 /opt/alerts/serve.py --test-push'
```

Do hodin se opíše `https://hodiny:$ALERTS_PASSWORD@$CLOCK_HOST/alerts`. Testy bez sítě
(z kořene repozitáře): `python3 -m unittest infra/alerts/test_serve.py`.

## Nastavení všech hodin přes server

`https://$FLEET_HOST/` je jedno místo pro nastavení všech hodin, i mimo
domácí síť. Po přihlášení je tam seznam hodin a u každé její vlastní stránka
nastavení — ta z firmwaru daného kusu, jen s výběrem hodin v hlavičce.

**Proč vlastní jméno.** Na `$CLOCK_HOST` běží v kořeni Home Assistant a jeho
service worker (`sw-modern.js`, scope `/`) obsluhuje v prohlížeči všechno na
tom jméně: stránky končící `/` vrací z mezipaměti a čerstvé stahuje až na
pozadí. Nastavení hodin i `/novinky/` pak byly o návštěvu pozadu (po
přihlášení se stránka nezměnila, starý token proti CSRF) a sdílely s HA
úložiště prohlížeče, kde HA drží přihlašovací tokeny. Proto `$FLEET_HOST`
(v `.env`, Caddy `{{FLEET_DOMAIN}}`, služba s `FLEET_PREFIX=` prázdným);
staré adresy `/fleet/…` a `/novinky/…` na `$CLOCK_HOST` přesměrují. Datové
kanály pro hodiny zůstávají na `$CLOCK_HOST`, hodiny nejsou prohlížeč.

**Jak to jde přes NAT.** Server v OCI se k hodinám doma nedovolá, proto se
hodiny připojují samy: drží jedno odchozí TLS spojení a ptají se
`GET /agent/poll` (server drží dotaz až 25 s). Když prohlížeč něco chce,
server to hodinám vrátí jako odpověď na dotaz, hodiny to pošlou svému vlastnímu
webu přes loopback (`127.0.0.1:80`) a výsledek vrátí `POST /agent/result`.
Server ke stránce jen přidá prefix `/d/<název>/` před adresy a výběr
hodin; firmware nic nevykládá, takže nové funkce stránky fungují přes server
samy. Hodiny, které se 45 s neozvaly, jsou offline. V domácí síti se nic
neotevírá a server adresy hodin nezná.

**Zabezpečení:**

- přihlášení heslem (scrypt) a kódem **TOTP** (Aegis, Google Authenticator…)
  v jednom kroku; kód nejde použít dvakrát, neúspěch neřekne, co nesedělo,
- brzda: 5 neúspěchů z jedné IP za 15 min, 30 celkem za hodinu → 15 min nic;
  pokusy jsou v journalu jako `fleet: failed login from <IP>`,
- relace jen v paměti (restart služby odhlásí), cookie `__Secure-fleet`
  HttpOnly/Secure/SameSite=Strict, 30 min nečinnosti, nejvýš 12 h,
- každý dotaz na API hodin nese token proti CSRF (hlavička `X-Fleet-Csrf`) —
  Home Assistant běží na stejném jménu, takže SameSite sám nestačí,
- hodiny se hlásí vlastním tokenem, server drží jen jeho SHA-256,
- přes server **nejde**: heslo webu hodin, nastavení vzdálené správy, ovládací
  API ani přihlášení do webu hodin — hlídá to server i firmware
  (`RemoteAdmin.cpp`). Firmware jde instalovat jen z oficiálního vydání,
- hodiny ověřují certifikát serveru (svazek kořenů Mozilly) a na vlastní web
  pouštějí jen dotazy s klíčem loopbacku, který se generuje při každém startu.

Co to znamená: kdo ovládne server nebo přihlášení, může přenastavit všechny
připojené hodiny a přečíst jejich nastavení (adresy s hesly, ne tokeny — ty
web nevrací). Proto TOTP a proto je správa v hodinách ve výchozím stavu
vypnutá a zapíná se jen doma.

Zavedení (jednou):

```sh
python3 infra/fleet/serve.py hash-password   # na Macu; FLEET_PASSWORD_HASH=...
python3 infra/fleet/serve.py new-totp        # FLEET_TOTP_SECRET=... + otpauth:// adresa
tools/deploy.sh --init fleet                 # uživatel, /opt/fleet, state/ 700, prázdný fleet.env
# oba řádky do /opt/fleet/fleet.env (root:root 600), pak:
ssh -i "$CLOCK_SSH_KEY" "$CLOCK_SSH" 'sudo systemctl restart fleet-web.service'
tools/deploy.sh caddy                        # blok {{FLEET_DOMAIN}}
```

Adresu `otpauth://` stačí převést na QR kód (`qrencode -t ansiutf8 '<adresa>'`)
a naskenovat, nebo tajemství opsat ručně. Nikam jinam ji neukládej.

Hodiny se přidávají na serveru; token se ukáže jen jednou:

```sh
ssh -i "$CLOCK_SSH_KEY" "$CLOCK_SSH" 'sudo -u fleet env FLEET_STATE=/opt/fleet/state python3.11 /opt/fleet/serve.py add-device pracovna'
```

Pak v hodinách **doma** Systém → **Vzdálená správa přes server**: adresa
`https://$FLEET_HOST`, token, zapnout, uložit. Webový server hodin musí
být „Vždy zapnutý“ (v režimu na 10 minut odpovídá po jejich uplynutí zamčeně).
Stav spojení je vidět tamtéž. `remove-device <název>` token okamžitě zneplatní
(`devices.json` se čte bez restartu), `list-devices` vypíše registrované.
`curl -s http://127.0.0.1:8099/fleet-status` na serveru ukáže, kdo je online.

Testy bez sítě: `python3 -m unittest infra/fleet/test_serve.py`; protokol
ve firmwaru `tools/run_host_tests.sh` (`remote_admin`). Stránku přes server jde
vyzkoušet i bez hodin: `tools/preview_web_ui.py` jako „hodiny“ a malý agent,
který jeho odpovědi posílá serveru (hlavička `X-Remote-Admin` přepne náhled
do režimu „otevřeno přes server“).

## Past s X-Forwarded-For (přečti dřív, než začneš „opravovat“ Caddyfile)

U Home Assistanta v `caddy/Caddyfile` kdysi stály tyhle dvě řádky:

```
header_up -X-Forwarded-For
header_up -X-Forwarded-Host
```

Od 8. 9. 2026 tam **nejsou** a nesmí se vrátit. Sekce zůstává kvůli tomu, aby
je někdo nepřidal znovu ve chvíli, kdy HA začne na proxovaný požadavek vracet
400 — to je totiž přesně ta reakce, kterou obcházely.

Vypadaly jako chyba, ale obcházely jinou chybu. Home Assistant v tomhle
kontejneru načítal `configuration.yaml` **nespolehlivě** — při jednom restartu
zahlásil „Unable to find configuration. Creating default one in /config“, při
jiném vzal `use_x_forwarded_for`, ale ne `trusted_proxies`. Pak považoval Caddy
za nedůvěryhodnou proxy a na každý proxovaný požadavek vracel **400**.
`check_config` přitom vždycky prošel; špatně byl jen běžící proces.

Bez těch hlaviček vidí HA obyčejný lokální požadavek a vrací 200. Daň je větší,
než vypadá: všechny požadavky se HA jeví jako z `127.0.0.1`, takže **nezná
skutečnou IP klienta a nemá podle čeho počítat neúspěšná přihlášení**. Ochrana
proti hádání hesla (`ip_ban`, brzdění opakovaných pokusů) klíčuje právě podle
IP adresy, takže na přihlašovací stránce vystavené do internetu prakticky
nefunguje — a kdyby se ban přece jen spustil, zabanuje `127.0.0.1`, tedy
všechny včetně hodin. Hodinám samotným, které se hlásí tokenem, to nevadí;
riziko neslo lidské přihlašování. Právě kvůli tomu se obchvat rušil; silné
heslo a dvoufaktor na HA ale dávají smysl tak jako tak.

**Proč to nebyl bind-mount (a proč recreate nepomůže).** Původně to tu stálo
jako závod při startu: HA prý sáhne po konfiguraci dřív, než se připojí
bind-mount. Změřeno 8. 9. 2026 to tak není a **recreate kontejneru problém
nevyřeší** — stav, o který jde, leží v připojeném `/config`, takže recreate
přežije.

Skutečná příčina: HA 2026.x přestěhoval nastavení `http:` z YAML do
`.storage/http` a **migruje ho právě jednou**. `homeassistant/components/http/
config.py` to říká natvrdo: *„YAML config is only migrated once. Subsequent
boots will ignore YAML and use the store exclusively."* Migrace tady proběhla
13. 8. 2026 při vzniku kontejneru, kdy `configuration.yaml` ještě žádný
`http:` blok neměl. Uložený `stable` proto nemá `use_x_forwarded_for` ani
`trusted_proxies` a `yaml_migration_done` je `true` — od té chvíle je celý
`http:` blok v YAML mrtvý text. HA na to samo upozorňuje: v Nastavení →
Opravy leží hlášení `yaml_still_present_after_migration`.

Pozná se to takhle (zevnitř, protože Caddy hlavičku zvenčí utne):

```sh
curl -s -o /dev/null -w '%{http_code}\n' \
     -H 'X-Forwarded-For: 203.0.113.9' http://127.0.0.1:8123/
```

`400` a v logu „*your HTTP integration is not set-up for reverse proxies*"
znamená `use_x_forwarded_for = False`. Stejnou kontrolu dělá
`tools/check-stack.sh --deep`.

**Oprava:** přimět HA, aby migraci provedl znovu nad aktuálním YAML. Kontejner
musí být zastavený, jinak si HA soubor přepíše z paměti:

```sh
C=/path/to/your/config/.storage/http
sudo docker stop homeassistant
sudo cp -a "$C" "$C.bak-$(date +%Y%m%d-%H%M%S)"
sudo sed -i 's/"yaml_migration_done": true/"yaml_migration_done": false/' "$C"
sudo docker start homeassistant
```

Po náběhu musí `curl` výš vrátit `200`. Teprve **potom** smaž obě řádky
`header_up` z Caddyfile a dej `sudo systemctl reload caddy`. To pořadí je
důležité: když se něco pokazí, Caddy je pořád nedotčený a HA jede dál.

**Kde nastavení bydlí teď.** Po migraci je zdrojem pravdy `.storage/http`,
který se edituje v **Nastavení → Systém → Síť**, ne v `configuration.yaml`.
HA po úspěšné migraci vyvěsí hlášení „The HTTP YAML configuration is
deprecated" a chce, aby se `http:` blok z YAML smazal; v 2027.2.0 přestane
YAML fungovat úplně. Smazat se smí až ve chvíli, kdy store hodnoty opravdu
má — ověř si to takhle, ne od oka:

```sh
sudo python3 -c "import json;print(json.load(open('/path/to/your/config/.storage/http'))['data']['stable'])"
```

Musí být vidět `use_x_forwarded_for: True` a `trusted_proxies`. Pak teprve
`http:` blok z `configuration.yaml` pryč a restart. **Bezpečné pořadí je
nejdřív úklid YAML a restart HA, teprve potom reload Caddy** — dokud Caddy
hlavičky utíná, tváří se každý požadavek jako lokální, takže ani rozbitý
store nezhodí web; kdyby se něco pokazilo, obnov zálohu a restartuj.

(Ta cesta `/path/to/your/config` je doopravdy takhle pojmenovaná — zástupný
text z návodu se kdysi nenahradil a HA na něm od té doby jede.)

## Rozvrh a úkoly ze Školy OnLine

Obrazovka **Škola** na hodinách ukazuje rozvrh jednoho dítěte a jeho domácí
úkoly, na druhé stránce (tažením prstu) nepřečtené zprávy a nové známky.
Hodiny se do Školy OnLine nepřihlašují: `school/serve.py` se přihlásí
sám, drží si token v paměti, stáhne rozvrh na dva týdny dopředu a aktivní
úkoly a hodinám vrací jen hotové řádky (`/school.json`, kolem kilobajtu).

**Kolik dotazů jde do Školy OnLine.** Je to cizí server a neoficiální API,
takže co nejméně. Každý zdroj se stahuje **nejvýš jednou za tři hodiny**.
Jedno stažení jsou dva dotazy (rozvrh, úkoly) a zhruba jednou za hodinu obnova
tokenu; dítě se v `/v1/user` hledá jednou denně. Intervaly zůstávají jako
samostatné proměnné, aby se daly měnit jednotlivě, i když jsou teď stejné:

| Situace | Interval | Proměnná |
| --- | --- | --- |
| 6–21 h, dnes do konce vyučování nebo od poledne před školním dnem | 3 h | `SCHOOL_DAY_POLL_MINUTES` |
| noc (21–22 h, 5–6 h) | 3 h | `SCHOOL_NIGHT_POLL_MINUTES` |
| jindy ve dne (pátek po škole, sobota, neděle dopoledne) | 3 h | `SCHOOL_IDLE_POLL_MINUTES` |
| v celých dvou týdnech ani hodina (prázdniny) | 6 h | `SCHOOL_HOLIDAY_POLL_MINUTES` |
| 22–5 h: žádný dotaz, ani opakování po chybě nebo po startu služby | — | `SCHOOL_QUIET_HOURS` |

Zprávy, známky a nástěnka se připojují k běžnému stažení, každá nejvýš jednou
za tři hodiny (`SCHOOL_MESSAGES_POLL_MINUTES`, `SCHOOL_MARKS_POLL_MINUTES`,
`NASEMS_POLL_MINUTES`), v noci jen poprvé po startu. Mezi 22. a 5. hodinou
nejde do Školy OnLine ani na nasems.cz nic. Týden vyučování tak dá kolem 140
dotazů do Školy OnLine (zhruba 20 denně včetně zpráv a známek) a kolem 23
na nasems.cz (3 denně); prázdninový den kolem 12, respektive 2. Čísla
vycházejí ze simulace týdne nad `poll_minutes` a `quiet_wait`, ne z odhadu —
po změně intervalů ji spusť znovu.
Chyba sítě nebo serveru se opakuje po 10, 20, 40… minutách, nejvýš po dvou
hodinách, a `Retry-After` u 429/503 se dodrží. Každý odstup se náhodně
prodlouží nebo zkrátí o až 10 % (`SCHOOL_POLL_JITTER_PERCENT`), takže dotazy
nechodí v přesném rytmu. Z hodin se nic z toho nastavit nedá: „Obnovovat
každých“ v záložce Škola říká jen, jak často se hodiny ptají tohohle serveru.

**Zdroj.** Škola OnLine veřejné API nemá, ale mobilní aplikace mluví s JSON
API na `https://aplikace.skolaonline.cz/solapi/api` (OAuth2 password grant,
`client_id=test_client`). Je neoficiální — zmapovaly ho
[Libre-SkolaOnline/API-docs](https://github.com/Libre-SkolaOnline/API-docs),
[hacs-calendar_skolaonline](https://github.com/elvisek2020/hacs-calendar_skolaonline)
a [resol](https://codeberg.org/resol/resol). Oproti škrabání webové aplikace
(jako [skola-online-stahovani-znamek](https://github.com/JakubAndrysek/skola-online-stahovani-znamek))
se nemění s každou úpravou vzhledu webu. Když se změní, opravuje se
`school/feed.py`; tvar `/school.json` zůstane a firmware se měnit nemusí.

Co server posílá a proč tak:

- **Které dny.** Pole `days` nese dva školní dny (`SCHOOL_DAY_COUNT`): dnešek,
  dokud neskončila poslední hodina (plus 15 minut), potom nejbližší další den
  s vyučováním, a za ním další školní den — večer se balí taška na zítřek,
  v pátek odpoledne svítí pondělí a úterý. Každý den nese i `end`, konec
  poslední hodiny, která se koná. Počítá se při každém dotazu, ne při stažení,
  takže se den přepne včas. O prázdninách je `days` prázdné a hodiny řeknou,
  že se neučí.
- **Suplování.** Původní hodina, za kterou přišla náhrada, se zahodí; náhrada
  má `state` 1 a krátkou poznámku. Odpadlá hodina má `state` 2.
- **Úkoly** s termínem od dneška na 14 dní, seřazené podle termínu. Úkol bez
  termínu nebo označený jako hotový se neposílá. Zkratka předmětu se doplní
  z rozvrhu, protože úkol často nese jen plný název.
- **Zprávy** (`messages`, `messageCount`): jen nepřečtené (`read: false`)
  z posledních 14 dní (`SCHOOL_MESSAGE_DAYS`), nejnovější první, nejvýš šest
  řádků; `messageCount` je celkový počet. Ze zprávy jde jen příjmení
  odesílatele bez titulů a titulek — tělo zprávy (HTML, často o dítěti)
  server hodinám neposílá. Starší nepřečtené zprávy se nepočítají: rodič je
  čte jinde a na hodinách by visely napořád. Seznam zprávy jako přečtené
  neoznačí (ověřeno 15. 9. 2026); detail zprávy se nevolá.
  Počty z `/v1/user/notifications` se nepoužívají, s příznaky `read` nesedí.
- **Známky** (`marks`, `markCount`) z posledních 14 dní (`SCHOOL_MARK_DAYS`):
  den, zkratka předmětu, známka a téma, nejvýš osm řádků.
- **Nástěnka školky** (`notices`, `noticeCount`) z
  [nasems.cz](https://nasems.cz/prihlaseno/nastenka), jen když je v
  `school.env` vyplněné `NASEMS_LOGIN` a `NASEMS_PASSWORD`. Web API nemá:
  server se přihlásí formulářem, drží si PHP session v cookie a čte HTML
  nástěnky (`div.podnadpis` s titulkem a časem, text v `div.nastenka_obsah`).
  Posílá oznámení z posledních 14 dní (`SCHOOL_NOTICE_DAYS`), nejvýš šest:
  den, titulek a začátek textu bez oslovení („Vážení rodiče,“); hodiny řádek
  zkrátí na šířku displeje. Stahuje se nejvýš jednou za tři hodiny
  (`NASEMS_POLL_MINUTES`), tedy asi 3 stažení denně; jedno je jeden dotaz,
  dokud drží session, po jejím vypršení dva až tři. Na displeji je pod
  známkami.
- **Obědy** (`meals`) pod rozvrhem místo úkolů: nejbližší dva dny, kdy se vaří
  (obvykle dnes a zítra, v pátek dnes a pondělí), u každého dne školní jídelna
  a školka, jeden řádek
  `{"when":"DNES","today":true,"who":"ZŠ","text":…}`. `today` říká, která jídla
  patří dnešku; odpoledne je hodiny podle vlastního nastavení schovají, aby
  zbyl jen zítřek. Rozhodují samy: odpověď je společná pro všechny hodiny
  v domácnosti a každé můžou mít nastavenou jinou hodinu.
  Jídelna běží na iCanteenu (`SCHOOL_CANTEEN_URL`, např.
  `https://jidelna.zsondrejov.cz/`), který jídelníček na tři týdny ukazuje
  i bez přihlášení — server nic neposílá a žádné heslo nepotřebuje. Bere se
  jen hlavní chod `Oběd1` (`SCHOOL_CANTEEN_MEAL`) bez polévky. Školka má
  jídelníček na [nasems.cz](https://nasems.cz/prihlaseno/jidelnicek) za
  stejným přihlášením jako nástěnka (`NASEMS_MENU=0` ho vypne) a bere se jen
  „Hlavní chod“. Stránka nese aktuální týden; pondělí se v neděli dotáhne
  jedním AJAXovým dotazem jako tlačítkem „Následující týden“. Alergeny
  a nápoje („ovocný čaj“, „voda“) se zahazují. Obojí se stahuje nejvýš
  jednou za 6 hodin (`SCHOOL_MEALS_POLL_MINUTES`), v noci jen poprvé po
  startu: kolem 3 dotazů denně na jídelnu a 3 až 4 na nasems.cz navíc.
  Den bez jídla se vynechá a hledá se o den dál, nejvýš týden dopředu: víkend,
  svátek i den, kdy jídelníček místo jídla píše „Státní svátek“ nebo
  „Ředitelské volno“. Kvůli pondělí se jídelníček školky na příští týden
  dotahuje už v pátek, ne až v neděli.
- **Úkoly vypnuté** (`SCHOOL_HOMEWORK=0`): server se na ně Školy OnLine vůbec
  neptá a klíč `homework` nepošle; hodiny pak nepíšou ani „ŽÁDNÉ ÚKOLY“.
  Kód zůstává, zapnutí je jen smazání proměnné a restart služby.
- Selže-li stažení zpráv nebo známek, rozvrh jede dál se staršími daty; po
  `SCHOOL_MAX_AGE_HOURS` se zahodí. `SCHOOL_MESSAGES=0` a `SCHOOL_MARKS=0`
  je vypnou úplně a hodiny druhou stránku nenabídnou.
- **Uložený stav.** Po každém úspěšném stažení se data zapíšou do
  `/var/lib/school/state.json` (jen pro uživatele `school`: nese jméno dítěte,
  rozvrh, úkoly, titulky zpráv a známky; token ne). Po restartu — třeba po
  noční aktualizaci v tichých hodinách — je server vydává hned a první dotaz
  do Školy OnLine počká, dokud data nezestárnou na svůj interval. Stav se
  zahodí, když se změnil účet nebo `SCHOOL_STUDENT`, nebo když je soubor
  poškozený. Čisté stažení hned: `sudo rm /var/lib/school/state.json` a restart.
- **`problem`** je neprázdný, když poslední stažení selhalo. Server pak dál
  vydává starší data, nejdéle ale 14 hodin (`SCHOOL_MAX_AGE_HOURS`, přečká
  noc bez dotazů); potom
  odpovídá 503 a hodiny místo včerejšího rozvrhu bez suplování ukážou
  „Server nemá čerstvý rozvrh ze Školy OnLine“. Firmware `problem` nečte,
  hlídá ho `tools/check-stack.sh`.

**Heslo do Školy OnLine** leží v `/opt/school/school.env` (root, 600) a nikam
jinam než na `aplikace.skolaonline.cz` nejde. Lepší je rodičovský účet než
žákovský: dítě si heslo může změnit. Po odmítnutém hesle server šest hodin
nic nezkouší, aby školní systém účet po sérii chyb nezamkl — po opravě hesla
proto `systemctl restart school-web.service`. Uložený stav restart nezdrží:
data z doby před chybou jsou starší než interval, takže se ptá hned.

**Heslo k `/school.json`** je vlastní (`SCHOOL_HASH` v `caddy.env`), stejně
jako u letadel a blesků: adresa opsaná do hodin nemá stačit na nic jiného.

Zavedení (jednou):

```sh
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" "$@"; }
openssl rand -hex 24               # do .env jako SCHOOL_PASSWORD
tools/deploy.sh --hash SCHOOL
tools/deploy.sh --init school
# Přihlášení: vzor je infra/school/school.env.example.
SSH -t "$CLOCK_SSH" 'sudo vi /opt/school/school.env'   # --init ho založil prázdný, 600 root
# Ověření, že účet vidí dítě a API vrací to, co feed.py čte (vypisuje skutečná data):
SSH -t "$CLOCK_SSH" 'sudo systemd-run --pty --quiet --uid=school -p EnvironmentFile=/opt/school/school.env /usr/bin/python3.11 /opt/school/serve.py --probe'
SSH "$CLOCK_SSH" 'sudo systemctl restart school-web.service'
tools/deploy.sh caddy
```

Do hodin se pak opíše
`https://hodiny:$SCHOOL_PASSWORD@$CLOCK_HOST/school.json` v záložce **Škola**.

## Nasazení změn z repozitáře

Soubory se mění tady a teprve pak jdou na server; opačné pořadí je přesně to,
co `--deep` ohlásí jako rozjeté. Co kam patří, je v `infra/manifest.txt`,
a nasazuje se jediným skriptem:

```sh
tools/deploy.sh rain               # jedna služba
tools/deploy.sh news agenda        # víc služeb
tools/deploy.sh --all              # všechno; co se nezměnilo, se nerestartuje
tools/deploy.sh --init alerts      # první zavedení: i uživatel, adresáře a prázdné *.env
tools/deploy.sh --hash RAIN        # RAIN_PASSWORD z .env → RAIN_HASH v caddy.env, restart Caddy
tools/deploy.sh --dry-run rain     # jen vypíše, co by se na serveru provedlo
tools/deploy.sh --list             # služby v manifestu
```

Co skript udělá:

1. Odmítne necommitnuté změny v `infra/` (`--allow-dirty` to přebije), protože
   by pak server neodpovídal žádnému commitu.
2. Nahraje soubory služby do `~/deploy-stage` a jako root je nainstaluje na
   místo: kód a jednotky `root:root 644`, `backup.sh` 700. Přímé `scp` do
   `/opt` by skončilo na právech, adresáře tam od 13. 9. 2026 `opc` nepatří
   (viz „Zabezpečení stroje“). Soubor, který se nezměnil, nechá být.
3. `daemon-reload`, a jen u služeb, kterým se nějaký soubor změnil, akce
   z manifestu: `restart` webové služby, `start` generátoru (jeden běh hned),
   `enable --now` timeru, `pip install` u družic. `--force` je provede i bez
   změny.
4. Nový Caddyfile (s dosazeným `{{DOMAIN}}`) dostane `reload`. Když reload
   spadne, **vrátí předchozí Caddyfile** a vypíše konec žurnálu. Jinak by na
   disku zůstala konfigurace, se kterou by Caddy nenaběhl po příštím restartu,
   třeba za měsíc při aktualizaci — přesně to se stalo 20. 9. 2026.
5. Porovná otisky nainstalovaných souborů s repem a pustí
   `tools/check-stack.sh --deep`.

**Nový hash v `caddy.env` potřebuje `restart`, ne `reload`.** Proměnné z
`EnvironmentFile` čte systemd jen při startu procesu, takže po přidání řádku
`*_HASH` je nový `{env.*_HASH}` pro běžící Caddy prázdný a konfigurace se
odmítne:

```
http_basic: account 0: username and password are required
```

Zní to, jako by byl špatně Caddyfile, ale chybí jen heslo v prostředí. Proto
`--hash` Caddy restartuje sám, a proto se u nové služby dělá **`--hash` dřív
než `tools/deploy.sh caddy`**. Běžná změna Caddyfile bez nového hesla si
vystačí s reloadem, který dělá `deploy.sh caddy`. Restart na známé dobré
konfiguraci je bezpečný; certifikáty zůstávají na disku.

**Nová služba** = adresář v `infra/`, řádky v `manifest.txt` (vzorem je
kterákoli služba, `dropin` řádek zapne hlášení poruch), heslo přes `--hash`,
`tools/deploy.sh --init <služba>`, blok v Caddyfile a `tools/deploy.sh caddy`.
`check-stack.sh --deep` začne soubory nové služby porovnávat sám.

Python: jednotky volají **`/usr/bin/python3.11`**, ne `python3` — ten je na
Oracle Linuxu 8 pořád ještě 3.6 a neumí ani `from __future__ import
annotations`, takže by se služba rozbila až za běhu. Letadla, blesky, srážky
a další si vystačí se standardní knihovnou; `.venv` mají jen zprávy, agenda
a družice.

`infra/caddy/Caddyfile` drží místo domény `{{DOMAIN}}`, aby adresa nebyla ve
veřejném repozitáři; `deploy.sh` i `check-stack.sh` ji dosazují z `CLOCK_HOST`.
Samostatné `caddy validate` tu schválně není: Caddyfile obsahuje `{env.*_HASH}`
a v obyčejném shellu ty proměnné nejsou, takže by validace spadla na prázdném
hesle. `systemctl reload` konfiguraci ověří sám.

Jednotky se službou (`news.service`, `news-web.service` …) leží na serveru ve
dvou místech: kopie v `/opt/<služba>/` je ta, kterou porovnává `--deep`, běhová
je v `/etc/systemd/system/`. `deploy.sh` zapisuje obě.

## Zabezpečení stroje

Stav k 13. 9. 2026. Zvenčí jsou otevřené jen porty z oddílu „Porty“.

**Každá služba má svého uživatele.** Dřív všechno běželo pod `opc`, který má
sudo bez hesla a SSH klíč; chyba v libovolné službě (generátor zpráv čte RSS
z internetu) by tak mohla skončit rootem. `NoNewPrivileges` sice sudo blokuje,
ale služba mohla přepsat kód ostatních nebo `~opc/.ssh/authorized_keys`.
Dnes platí:

| Adresář | Vlastník | Služba smí zapisovat |
|---|---|---|
| `/opt/news` | `root:news` 750 | `www/`, `state/` (`news`) |
| `/opt/agenda` | `root:agenda` 750 | `www/` (`agenda`); `key.json` je `agenda` 400 |
| `/opt/planes` | `root:planes` 750 | nic |
| `/opt/settings` | `root:settings` 750 | `data/` (`settings`) |
| `/opt/school` | `root:school` 750 | nic; `school.env` je `root` 600 |
| `/var/lib/school` | `school` 700 (zakládá systemd, `StateDirectory`) | `state.json` (`school` 600) |
| `/opt/satellites` | `root:satellites` 750 | nic; `.venv` patří rootovi |
| `/var/cache/satellites` | `satellites` 750 (zakládá systemd, `CacheDirectory`) | stažené skupiny drah |
| `/opt/health` | `root:health` 750 | nic; `health.env` je `root` 600 |
| `/var/lib/health` | `health` 700 (`StateDirectory`) | co už bylo nahlášené |
| `/opt/watch`, `/opt/ou-watch` | `root:watch` / `root:ouwatch` 750 | nic; kód, `.venv` i `watches.toml` patří rootovi |
| `/var/lib/watch`, `/var/lib/ou-watch` | `watch` / `ouwatch` 750 (`StateDirectory`) | `state.db`, fotky |

Kód a `.venv` patří rootovi, takže je služba nezmění. `news.env`
a `agenda.env` jsou rootovy (600): systemd je načte do prostředí ještě před
přepnutím na uživatele, ale jako soubor je služba nepřečte. Python kvůli tomu
nemůže zapsat `__pycache__` vedle kódu, což nevadí. Hlídače obchodů a obce (repozitáře `hlidac-novinek`, `hlidac-ondrejov`) mají
uživatele `watch` a `ouwatch` a od 22. 9. 2026 stejné rozdělení: dřív jim
patřil celý `/opt/watch` včetně `watch.py`, protože SQLite v režimu WAL potřebuje
zapisovat do adresáře s databází. Služba, která čte HTML z cizích webů, tak
mohla chybou v parseru přepsat vlastní kód natrvalo. Databáze a fotky jsou
proto ve `/var/lib/…` (`WATCH_STATE_DIR` v jednotce) a kód patří rootovi;
nasazuje se z jejich repozitářů (`tools/deploy.sh` tam).

**SSH:** jen klíčem, `PermitRootLogin no`, `X11Forwarding no`. Pokusů
o přihlášení chodilo kolem 38 000 týdně, takže běží **fail2ban** s jailem
`sshd` (`/etc/fail2ban/jail.d/sshd.local`: 5 pokusů za 10 min, ban hodina
a s každým opakováním déle, nejvýš týden). Balíky jsou z `ol8_developer_EPEL`,
který zůstává **vypnutý** a zapnul se jen pro tu instalaci
(`dnf --enablerepo=ol8_developer_EPEL`); aktualizace fail2ban tedy nepřijdou
samy. Odbanování: `sudo fail2ban-client set sshd unbanip <IP>`.

**Aktualizace:** `dnf-automatic.timer` denně instaluje bezpečnostní opravy
(`upgrade_type = security` v `/etc/dnf/automatic.conf`), sám nerestartuje.
Jádro za běhu záplatuje Ksplice (`autoinstall = yes`
v `/etc/uptrack/uptrack.conf`). Docker CE je 26.1.3: pro EL8 Docker novější
nevydává, a jeho repozitář nenese bezpečnostní hlášení, takže ho
`dnf updateinfo` ani `dnf-automatic` nevidí.

**Vypnuto:** `rpcbind` (port 111, nic ho nepoužívalo).

**Caddy** posílá `Strict-Transport-Security`, bez `includeSubDomains`.

Zálohy původních souborů z 13. 9. 2026 jsou v `/root/hardening-20260913/`.

**Home Assistant** běží od 13. 9. 2026 bez `--privileged` (dřív by průnik do
HA, jediné aplikace vystavené do internetu, znamenal root na celém stroji)
a vlastník má dvoufaktor (TOTP). Kontejner se zakládá takhle:

```sh
sudo docker run -d --name homeassistant --restart=unless-stopped \
  --network=host -e TZ=Europe/Prague \
  -v /path/to/your/config:/config \
  ghcr.io/home-assistant/home-assistant:stable
```

Hlášku „Missing required permissions for Bluetooth management“ v logu HA je
možné ignorovat: stroj v cloudu Bluetooth nemá a `NET_ADMIN`/`NET_RAW` se
kvůli ní přidávat nemají.

**Aktualizace Home Assistanta** nejdou samy: kontejner jede z tagu `:stable`,
Docker CE 26.1.3 pro EL8 novější nevydává a `dnf-automatic` ani jedno nevidí.
O nové verzi dá vědět `ha-update.timer` (v `infra/health/`): každé pondělí
v 07:40 UTC stáhne `:stable`, a když je novější než běžící kontejner, pošle na
téma hlášení poruch jeden push s odkazem na poznámky k vydání — jednou za
verzi, ne každý týden. Běží jako root, protože se ptá Dockeru; stav je ve
`/var/lib/ha-update`. Stažený image se neztratí, `--apply` pak nic nestahuje,
a starší nepoužitý `:stable` se hned uklidí. Samotné povýšení zůstává ruční,
protože nová verze může převést databázi:

```sh
tools/update-ha.sh           # stáhne :stable a řekne, jestli je novější; HA nechá běžet
tools/update-ha.sh --apply   # záloha, nový kontejner, čeká na 200; jinak vrátí předchozí image
```

Předtím přečíst „Breaking changes“ v poznámkách k vydání. Návrat na předchozí
image nevrátí data, která nová verze při startu převedla (databáze,
`.storage`) — k tomu je archiv, který `--apply` vyrobí jako první krok (obnova
viz „Zálohy dat“). Konfigurační adresář bere skript z běžícího kontejneru.
Pro návrat zůstává jen image, ze kterého se naposledy povyšovalo
(`homeassistant-previous:<verze>`, 2,3 GB); starší se při dalším `--apply`
smažou. Jednou za měsíc stačí. Naposledy 22. 9. 2026: 2026.8.1 → 2026.9.3 bez
potíží (stejných 114 entit, proxy s `X-Forwarded-For` v pořádku).

**SSH klíč** je od 22. 9. 2026 ed25519 místo původního RSA z OCI (viz
„Server“). Starý `authorized_keys` s RSA klíčem leží na serveru jako
`~opc/.ssh/authorized_keys.bak-20260922`.

## Zálohy dat

Tenhle adresář umí obnovit **software**. Neumí obnovit **data** — a několik
věcí na serveru nemá kopii nikde jinde:

| Co | Kde | Proč to jinde není |
|---|---|---|
| Konfigurace Home Assistanta | `/path/to/your/config` (bind-mount kontejneru) | `configuration.yaml`, `secrets.yaml`, automatizace a hlavně `.storage` — registr entit, uživatelé, tokeny a integrace naklikané v UI. Bez `.storage` je obnovené HA prázdné, i když YAML sedí. |
| Historie senzorů | `home-assistant_v2.db` | Grafy a statistiky. Šablonové senzory na ní nestojí. |
| Stav hlídačů | `/var/lib/watch/state.db`, `/var/lib/ou-watch/state.db` | Co už hlídač viděl. Bez toho přijde po restartu buď záplava „novinek“, nebo se dávka tiše ztratí. |
| Hesla a klíče | `caddy.env`, `news.env`, `agenda.env`, `key.json`, `school.env`, `alerts.env`, `health.env`, obě `watch.env`, `backup.env` (adresa PAR) | Do veřejného repozitáře nepatří. Většina se dá vyrobit znovu, `key.json` se vydá nový ve stejném projektu Google Cloudu (sdílení kalendářů zůstává). |

Naopak se zálohovat nemusí: dráhy družic a registr poloh zpráv se stáhnou
samy, certifikáty si Caddy vyžádá znovu přes ACME a kód služeb je v `infra/`
a v repozitářích hlídačů (`hlidac-novinek`, `hlidac-ondrejov`).

**Na serveru** běží `backup.timer` každou noc ve 03:20 UTC — mimo okna
`news.timer` i `agenda.timer`, aby se snímky sqlite nepraly o zámek.
`backup.sh` složí archiv do `/opt/backup/data/` (práva 700, archivy 600)
a smaže, co je starší než `BACKUP_KEEP_DAYS` (7 dní). Prořezává se podle
**stáří, ne podle počtu** — při ručním spuštění během dne by počítání
nejnovějších ubralo i archivy mladší než týden. `BACKUP_KEEP_MIN` (3) je
pojistka: samotné „smaž starší než 7 dní“ by při delším výpadku smazalo
i poslední zálohu, kterou máme, zrovna ve chvíli, kdy už žádná nová nevzniká.
Nejnovější tři kusy proto přežijí bez ohledu na věk. Živé databáze se kopírují přes
`sqlite3 .backup`, ne `cp`: prostý `cp` za běhu utrhne stránku uprostřed
transakce a výsledek je nepoužitelný. Každý snímek se ověří
`PRAGMA integrity_check` a výsledek je vidět v `MANIFEST` uvnitř archivu.

**Stáhnout pryč je ta důležitá část.** Archiv na stejném disku jako originál
neochrání před ničím kromě vlastního `rm`:

```sh
tools/pull-backup.sh          # stáhne nejnovější archiv sem
tools/pull-backup.sh --run    # nejdřív spustí zálohu na serveru, pak stáhne
tools/pull-backup.sh --list   # jen vypíše, co na serveru leží
```

Stahuje do `BACKUP_LOCAL_DIR` z kořenového `.env` (jinak `~/waveshare-zalohy`),
adresář dostane 700 a archiv 600, a po stažení ověří, že jde rozbalit. Prořezává
se stejně jako na serveru — `BACKUP_LOCAL_KEEP_DAYS` (7) a `BACKUP_LOCAL_KEEP_MIN`
(3).

### Čím je archiv chráněný

**Archiv nese hesla a klíče v otevřené podobě** — heslo do Školy OnLine, klíč
Gemini, servisní účet Google, tokeny Home Assistanta. Je to nejhustší snůška
tajemství, jakou kolem hodin máme, a stojí na čtyřech věcech:

| Vrstva | Co kryje |
|---|---|
| Práva 700/600, vlastník root | Ostatní účty na serveru i na Macu |
| SSH | Přenos |
| FileVault na Macu | Ukradený nebo vypnutý notebook |
| Vyloučení z Time Machine | Záložní disk, který bývá nešifrovaný a nosí se z domu |

Poslední řádek nastavuje `pull-backup.sh` sám při každém běhu. Není to
nastavení Time Machine, ale `xattr` na adresáři — když adresář někdo smaže
a založí znovu, vyloučení zmizí, proto se kontroluje pokaždé. Ověřit jde
`tmutil isexcluded ~/waveshare-zalohy`.

Archivy na serveru a na Macu se **nešifrují**, a je to vědomé rozhodnutí.
U noční úlohy bez obsluhy by heslo muselo ležet na serveru vedle toho, co
šifruje, takže by nekrylo nic. Ztráta klíče by přitom znamenala ztrátu všech
archivů najednou — horší riziko než to, které by šifrování řešilo na dvou
strojích, které stejně chrání práva a FileVault.

Kopie, která odchází **mimo oba stroje**, se šifruje vždycky (viz „Kopie mimo
stroj“). Totéž by platilo pro každou další zálohovací službu na Macu (Backblaze
a spol.): `~/waveshare-zalohy` vyloučit stejně jako u Time Machine, jinak
tajemství odtečou tam nešifrovaná.

### Kopie mimo stroj

Mac a server můžou zmizet najednou — ukradený notebook a zrušený účet, požár,
omylem smazaná instance. Proto `backup.sh` po každé noční záloze pošle
zašifrovanou kopii do **OCI Object Storage** (Frankfurt, bucket
`hodiny-zalohy`):

- **Šifruje se veřejným klíčem** `infra/backup/offsite-key.asc` (GnuPG, ed25519
  + cv25519, otisk `9393 76ED 89E5 C11D B2B6  CBA6 3BEA 33FF 721A 5588`), na
  serveru jako `/opt/backup/offsite-key.asc`. **Soukromý klíč na serveru
  není**: kdo stroj ovládne, starší kopie nerozšifruje. Leží na Macu
  v `~/.config/hodiny-backup/offsite-secret.asc` a v záloze
  `~/Documents/oracleKeys/hodiny-backup/`. Bez něj jsou kopie k ničemu — patří
  i do správce hesel. Klíč je bez hesla, stejně jako SSH klíč vedle něj.
  Server má GnuPG 2.2 a Mac 2.5; klíč je ve formátu v4, který umí oba
  (ověřeno oběma směry 22. 9. 2026). Nový klíč z GnuPG 2.5 by měl vyrobit
  totéž: `--quick-gen-key … ed25519 cert`, pak `--quick-add-key … cv25519 encr`.
- **Nahrává se přes pre-authenticated request** (PAR) s právem jen zapisovat
  objekty. Server tedy kopie nevidí, nemůže je vypsat ani smazat, jen přidávat.
  Adresa PAR nese tajný token, je jen v `/opt/backup/backup.env` (600) jako
  `BACKUP_OFFSITE_URL` a kopie v kořenovém `.env`; do logu se nedostane.
  PAR má datum vypršení — po něm nahrávání selže, `backup.service` skončí
  chybou a přijde push. Pak stačí v konzoli vydat nový a přepsat adresu.
- **Mazání** obstarává pravidlo životního cyklu v bucketu (objekty starší než
  30 dní). Jedna kopie má kolem 12 MB, měsíc tedy asi 360 MB; Always Free kryje
  20 GB.
- Když nahrání selže, místní záloha už je hotová a prořezaná; jednotka jen
  skončí chybou, aby o tom přišel push.

Zřízení v konzoli OCI (jednou): **Storage → Buckets → Create Bucket**
`hodiny-zalohy` (Standard, soukromý); v bucketu **Lifecycle Policy Rules →
Create Rule** „Delete“, objekty starší než 30 dní; **Pre-Authenticated Requests →
Create**, cíl *Bucket*, *Permit object writes*, bez výpisu objektů, s dlouhým
vypršením. Adresa se ukáže jen jednou; končí na `/o/` a patří do `.env` jako
`BACKUP_OFFSITE_URL` a do `/opt/backup/backup.env`.

**Obnova z kopie:** v konzoli stáhnout objekt (`majnr/server-….tar.gz.gpg`), pak

```sh
tools/decrypt-backup.sh ~/Downloads/server-20260923-032000.tar.gz.gpg
```

Skript naimportuje soukromý klíč do dočasné klíčenky (běžný GnuPG na Macu
zůstane nedotčený), rozšifruje a ověří, že je to archiv zálohy; dál podle
„Obnova z archivu“. Potřebuje `brew install gnupg`.

### Denní stahování na Macu

Aby na to nikdo nemusel myslet, stahování obsluhuje launchd:

```sh
tools/install-pull-backup-agent.sh              # nainstaluje a spustí
tools/install-pull-backup-agent.sh --status     # jak si stojí + konec logu
tools/install-pull-backup-agent.sh --uninstall  # odebere
```

Agent `cz.majnr.hodiny.pull-backup` běží každý den v 10:00 (hodinu změní
`PULL_BACKUP_HOUR`). Server sype archiv ve 03:20 UTC, takže dopoledne je
vždycky hotový. Když Mac v tu dobu spí nebo je vypnutý, launchd úlohu spustí
jednou hned po probuzení — „jednou denně, když Mac běží“ tedy platí i pro
stroj, který přes noc nesvítí. Log je v `~/Library/Logs/hodiny-pull-backup.log`.

Plist se generuje instalačním skriptem a v repozitáři neleží: nese absolutní
cestu k tomuhle klonu, která je na každém stroji jiná — stejně jako `{{DOMAIN}}`
v Caddyfile.

**Past: SSH klíč v `~/Documents` (přečti, než začneš „opravovat“ launchd).**
Agent na klíč v `~/Documents`, `~/Desktop` nebo `~/Downloads` **nedosáhne**.
Tyhle adresáře hlídá macOS TCC a úloha z launchd nedědí povolení, které má
Terminál, takže `ssh` skončí na `Load key ...: Operation not permitted`
a hned za tím `Permission denied (publickey)`. Ručně týž skript projde, což
vede k hledání chyby na špatném místě. Klíč proto patří do `~/.ssh`
(`CLOCK_SSH_KEY=$HOME/.ssh/majnr-oracle-ed25519`) — ten adresář TCC nehlídá.
Rozšiřovat Full Disk Access na `/bin/bash` kvůli jednomu klíči je horší lék
než nemoc.

Nastavení je v `/opt/backup/backup.env`, vzor je `backup/backup.env.example`.
Svět Minecraftu tam schválně není: je řádově v GB a mění se pořád, takže patří
do `BACKUP_EXTRA` jen s vědomím, o kolik každý archiv naroste.

**Obnova z archivu:** rozbal ho a vrať soubory z `files/` na stejné cesty,
práva podle oddílu „Zabezpečení stroje“ (hesla 600, `key.json` 600 vlastník
`agenda`). Kód služeb v archivu není, ten se nasadí z repozitáře.

## Kontrola

```sh
tools/check-stack.sh          # zvenčí: kanál, hesla, obsah všech zdrojů, HA, certifikát — bez SSH
tools/check-stack.sh --deep   # + jednotky (i hlídačů), selhané jednotky, hlášení poruch, shoda s manifestem
```

Nahrazuje pamatování si diagnostického postupu. Když hodiny neukazují zprávy,
skript řekne, jestli je vadný kanál, generátor, proxy nebo certifikát. Hesla
bere z kořenového `.env` (`*_PASSWORD`); které chybí, to se jen přeskočí
s `warn`. Soubory porovnává podle `infra/manifest.txt`, takže nová služba se
do kontroly nepřidává zvlášť.

`systemctl --failed` má být v běžném provozu prázdné — `--deep` hlásí každou
selhanou jednotku, i tu mimo `infra/`. Po ladění přes `systemd-run` jich
zůstávají celé řady (`run-u7585.service` …), a mezi nimi by se skutečná
porucha ztratila. Po opravě `sudo systemctl reset-failed`.

## Hlášení poruch

`check-stack.sh` pomůže, jen když ho někdo pustí. Do 22. 9. 2026 se tak přišlo
na timeout `news.service` (16. 9.) i na spadlý reload Caddy (20. 9.) jen
náhodou. Od té doby hlídá server sám sebe a posílá push přes ntfy — na
**vlastní téma**, ne na to od upozornění na déšť ani od hlídání obchodů, aby
se dalo v aplikaci ztlumit zvlášť. Téma je v `/opt/health/health.env` (600
root) a kopie v kořenovém `.env` jako `HEALTH_NTFY_TOPIC`; v aplikaci ntfy
**Subscribe to topic** s tímhle řetězcem.

Dvě cesty, obě v `infra/health/`:

- **Selhaná jednotka.** Každá hlídaná jednotka má drop-in
  `/etc/systemd/system/<jednotka>.d/on-failure.conf` s
  `OnFailure=notify-failure@%n.service`, a ta pošle jméno jednotky a posledních
  12 řádků žurnálu. Tatáž jednotka nejvýš jednou za 30 minut, aby smyčka
  restartů nezahltila telefon. Drop-in se nasazuje z `manifest.txt` (řádky
  `dropin`), takže jednotky samotné — ani ty z repozitářů hlídačů — se kvůli
  tomu neměnily. Dlouhoběžící služba s `Restart=on-failure` se do stavu failed
  dostane až po vyčerpání restartů; jednotlivé pády zachytí hodinová kontrola.
- **Hodinová kontrola** (`health.timer`, v :52) se po loopbacku, bez Caddy
  a bez hesel, zeptá každé služby na její stav: stáří zpráv (14 h) a agendy
  (1 h), `problem` u školy, `/status` srážek, výstrah, družic a upozornění,
  odpověď úložiště záloh, hlídače obchodů a HA. Dál projde `systemctl --failed`,
  jestli jednotky z `HEALTH_UNITS` běží, kdy naposledy spustily timery,
  certifikát (méně než 21 dní) a zaplnění disku (víc než 85 %). Nic z toho
  nestahuje od cizích zdrojů. Problém musí vydržet **dva běhy po sobě**, než
  odejde push, a když zmizí, přijde jeden „vyřešeno“. Neodeslaný push se zkusí
  při dalším běhu.

Reload Caddy, který spadne, jednotku do stavu failed nedostane (běží dál stará
konfigurace). To hlídá `tools/deploy.sh`, který v tom případě vrátí předchozí
Caddyfile a skončí chybou.

```sh
ssh … 'sudo systemctl start health.service; sudo journalctl -u health -n 5 -o cat'   # co vidí teď
ssh … 'sudo cat /var/lib/health/checks.json'                                         # co už nahlásil
ssh … 'sudo -u health env $(sudo cat /opt/health/health.env) python3.11 /opt/health/check.py --test-push'
```

Zkouška celé cesty přes `OnFailure=` (vyzkoušeno 22. 9. 2026):

```sh
ssh … 'sudo systemd-run --unit=health-selftest -p OnFailure=notify-failure@health-selftest.service.service /bin/false; sleep 5; sudo systemctl reset-failed'
```

Testy bez sítě: `python3 -m unittest infra/health/test_check.py`.

## Obnova serveru

1. Nový stroj, otevřít 80/443 v OCI i ve `firewalld`. `dnf install
   python3.11 sqlite`, Docker podle „Zabezpečení stroje“.
2. Caddy: binárku do `/usr/bin/caddy` (`restorecon -v /usr/bin/caddy`), uživatel
   `caddy`, `HOME=/var/lib/caddy`, `/etc/caddy/caddy.env` s právy 600 — nejlíp
   rovnou z posledního archivu (nese všechny `*_HASH`), jinak `tools/deploy.sh
   --hash JMÉNO` pro každé heslo z `.env`. Bez `caddy.env` se Caddy nespustí,
   viz „Heslo k agendě“.
3. Hesla a klíče z posledního archivu na jejich místa (`news.env`,
   `agenda.env`, `key.json`, `school.env`, `alerts.env`, `health.env`,
   `backup.env`, obě `watch.env`) s právy podle „Zabezpečení stroje“. Archiv je
   na Macu v `~/waveshare-zalohy`; když chybí i Mac, stáhne se kopie z bucketu
   `hodiny-zalohy` a rozšifruje přes `tools/decrypt-backup.sh` (viz „Kopie mimo
   stroj“). `tools/deploy.sh --init`
   prázdné `*.env` založí, ale nikdy nepřepíše vyplněné.
4. `tools/deploy.sh --init --all`. Založí uživatele a adresáře, nainstaluje
   všechno z manifestu včetně drop-inů hlášení poruch a jednotky zapne.
   `.venv` pro zprávy (`google-genai`, `feedparser`, `pydantic`) a agendu
   (`google-auth`, `requests`) se staví ručně přes **`/usr/bin/python3.11 -m
   venv`**; manifest je zakládá jen u družic.
5. Hlídače obchodů a obce: `tools/deploy.sh` v jejich repozitářích, pak
   `state.db` z archivu do `/var/lib/watch/` a `/var/lib/ou-watch/` (vlastník
   `watch`, resp. `ouwatch`). Bez databáze by první běh jen potichu zasel
   základ — nic se neztratí, ale nic, co přibylo mezitím, se neohlásí.
6. Home Assistant: konfiguraci z archivu, kontejner podle „Zabezpečení stroje“.
7. `/opt/backup/backup.env` (600) z archivu, jinak z `backup/backup.env.example`
   s cestou ke konfiguraci HA a adresou PAR (`BACKUP_OFFSITE_URL`; při
   podezření, že ji zná někdo cizí, vydat v konzoli novou a starou smazat).
   Na Macu `tools/install-pull-backup-agent.sh`, ať se stahuje dál samo.
8. `tools/check-stack.sh --deep` a `ssh … 'sudo systemctl start health.service'`.

**Python na tom stroji:** `python3` je 3.6.8, který nemá `zoneinfo` a tiše
nainstaluje roky staré verze knihoven. Všechny `.venv` se proto stavějí výslovně
`/usr/bin/python3.11`.

**SELinux:** binárka přesunutá z `/tmp` si nese label `user_tmp_t` a systemd ji
odmítne spustit s `203/EXEC Permission denied`. Léčí to `restorecon -v
/usr/bin/caddy`. Platí pro cokoli nahraného přes `/tmp`.

**Modely Gemini:** pinované `gemini-2.5-flash` a `-flash-lite` vracejí 404 („no
longer available to new users“), Gemini 3.x odmítá `thinking_config.
thinking_budget=0` s 400. Proto se používá alias `gemini-flash-latest` a žádná
konfigurace thinkingu se neposílá. Přetížení (503) je běžné, každý model se
zkouší dvakrát a pak se jde na záložní. Vyčerpaná kvóta (429) se neopakuje
a jde se rovnou na další model; ten si běh pamatuje a na zbylé polohy ho už
nezkouší. Free tier má u `gemini-flash-latest` jen **20 dotazů denně** (běh
je jeden společný výběr plus jeden za polohu, osm běhů denně to přečerpá
kolem poledne), `gemini-flash-lite-latest` 500. Ráno proto vybírá Flash
a zbytek dne Lite. Limity jsou vidět v AI Studiu na stránce Usage.
