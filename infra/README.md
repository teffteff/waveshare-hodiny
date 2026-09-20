# Serverová část hodin

Hodiny samy o sobě žádný server nepotřebují, ale několik obrazovek ho používá:
zprávy si tahají RSS kanál, který na serveru generuje jazykový model, agenda
čte Google Kalendář a hodnoty chodí z Home Assistanta. Všechno jde přes HTTPS
reverzní proxy.

Tenhle adresář je **kopie toho, co na serveru opravdu běží**. Když se server
ztratí, dá se z něj složit znovu; když ho někdo změní bez commitu, ohlásí to
`tools/check-stack.sh --deep`. Soubory sem patří byte na byte stejné jako na
serveru — jediná schválená výjimka je `news/news.env.example`, viz níž.

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

Klíč bývá RSA, takže novější OpenSSH ho bez pomoci odmítne — proto všude
`-o PubkeyAcceptedAlgorithms=+ssh-rsa`:

```sh
ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$CLOCK_SSH"
```

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
| Družice | 8095, jen loopback | `/opt/satellites/serve.py`, `requirements.txt`, `.venv`, `satellites-web.service`, dráhy v `/var/cache/satellites/` | `satellites/` |
| Srážková předpověď | 8096, jen loopback | `/opt/rain/serve.py`, `rain-web.service` | `rain/` |
| Noční záloha dat | — | `/opt/backup/backup.sh`, `backup.service` + `backup.timer`, archivy v `/opt/backup/data/` | `backup/` |
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
https://hodiny:$SETTINGS_PASSWORD@$CLOCK_HOST/settings  zálohy nastavení (nepovinné)
https://hodiny:$AGENDA_PASSWORD@$CLOCK_HOST/agenda.json  agenda z kalendáře
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
8090, 8092, 8093, 8094, 8095 a 8096 mezi ně nepatří**: servery se zprávami, agendou, letadly,
blesky, zálohami, rozvrhem, družicemi a srážkami poslouchají jen na `127.0.0.1`, protože jinak by šlo heslo
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
NEW="$(openssl rand -hex 24)"   # hex schválně: bez : a @ kvůli adrese, bez $ kvůli .env
ssh … "caddy hash-password --plaintext '$NEW'"
# Jen řádek AGENDA_HASH: caddy.env nese i SETTINGS_HASH a WATCHER_HASH a bez
# nich by reload Caddy spadl. sed -i zachová práva 600.
ssh … "sudo sed -i 's|^AGENDA_HASH=.*|AGENDA_HASH=<hash>|' /etc/caddy/caddy.env"
ssh … "sudo systemctl restart caddy"
```

Restart, ne reload: proměnnou z `caddy.env` dostane běžící proces jen při
startu, takže po reloadu by dál platil starý hash. Nakonec se nové heslo opíše do `.env` a do hodin — dokud
se nezmění tam, obrazovka s agendou zůstane prázdná a `check-stack.sh` to
ohlásí.

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
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
NEW="$(openssl rand -hex 24)"      # hex: bez : a @ kvůli adrese
HASH="$(SSH "$CLOCK_SSH" "caddy hash-password --plaintext '$NEW'")"
# Hash se do caddy.env PŘIDÁ, AGENDA_HASH musí zůstat.
SSH "$CLOCK_SSH" "printf 'SETTINGS_HASH=%s\n' '$HASH' | sudo tee -a /etc/caddy/caddy.env >/dev/null && sudo chmod 600 /etc/caddy/caddy.env"
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/settings --shell /sbin/nologin settings \
    && sudo install -d -o root -g settings -m 750 /opt/settings \
    && sudo install -d -o settings -g settings -m 700 /opt/settings/data'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/settings/serve.py infra/settings/settings-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py settings-web.service /opt/settings/ \
    && sudo cp /opt/settings/settings-web.service /etc/systemd/system/ \
    && sudo systemctl daemon-reload && sudo systemctl enable --now settings-web.service'
```

Pak **`sudo systemctl restart caddy`** – ještě se starým Caddyfile – a teprve
potom nový Caddyfile (postup v „Nasazení změn z repozitáře“). `{env.…}` čte
běžící proces Caddy a proměnné z `caddy.env` dostane jen při startu; `reload`
spouští jen klienta, který konfiguraci předá dál. Bez restartu reload spadne
na prázdném `SETTINGS_HASH` (stalo se při nasazení 13. 9. 2026) – běžet zůstane
stará konfigurace, takže se nic nerozbije, ale ani nezapne. Restart na známé
dobré konfiguraci je bezpečný; certifikáty zůstávají na disku. Nakonec
`NEW` do kořenového `.env` jako `SETTINGS_PASSWORD`, aby ho četl
`tools/check-stack.sh`, a do hodin adresa
`https://hodiny:$NEW@$CLOCK_HOST/settings`.

Výměna hesla je stejná jako u agendy, jen se `sed` nahradí řádek `SETTINGS_HASH`
– a platí totéž: nový hash se projeví až po `restart`, ne po `reload`. Hodiny si
novou adresu uloží po prvním úspěšném spojení.

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

Zavedení (jednou), stejně jako letadla: uživatel, adresář, jednotka, pak hash
a nový Caddyfile.

```sh
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/lightning --shell /sbin/nologin lightning \
    && sudo install -d -o root -g lightning -m 750 /opt/lightning'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/lightning/serve.py infra/lightning/lightning-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py lightning-web.service /opt/lightning/ \
    && sudo cp /opt/lightning/lightning-web.service /etc/systemd/system/ \
    && sudo systemctl daemon-reload \
    && sudo systemctl enable --now lightning-web.service'
HASH="$(SSH "$CLOCK_SSH" "caddy hash-password --plaintext '$LIGHTNING_PASSWORD'")"
SSH "$CLOCK_SSH" "printf 'LIGHTNING_HASH=%s\n' '$HASH' | sudo tee -a /etc/caddy/caddy.env >/dev/null && sudo chmod 600 /etc/caddy/caddy.env"
```

Pak Caddy **restartovat** (nový hash se po `reload` nenačte) a teprve potom
nasadit Caddyfile postupem z „Nasazení“. Nasazeno 14. 9. 2026;
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
2. Hash na server a restart Caddy — ještě se starým Caddyfile (proč restart,
   viz „Zálohy nastavení“):

   ```sh
   set -a; . ./.env; set +a
   SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
   HASH="$(SSH "$CLOCK_SSH" "caddy hash-password --plaintext '$PLANES_PASSWORD'")"
   # Hash se do caddy.env PŘIDÁ, ostatní řádky musí zůstat.
   SSH "$CLOCK_SSH" "printf 'PLANES_HASH=%s\n' '$HASH' | sudo tee -a /etc/caddy/caddy.env >/dev/null && sudo chmod 600 /etc/caddy/caddy.env"
   SSH "$CLOCK_SSH" 'sudo systemctl restart caddy'
   ```

3. Nový Caddyfile podle „Nasazení změn z repozitáře“.
4. `tools/check-stack.sh --deep`: bez hesla 401, s heslem seznam letadel.

Výměna hesla je stejná jako u agendy, jen se `sed` nahradí řádek `PLANES_HASH`
a nová adresa se opíše do hodin.

## Družice

Obrazovka **Družice** ukazuje oblohu nad hodinami. Hodiny se ptají
`GET /satellites.json?lat=…&lon=…&groups=…&minel=…` a `satellites/serve.py`
jim vrátí pro každou družici nad obzorem azimut a výšku v desetinách stupně po
15 s na 3 minuty dopředu (`p`), výšku dráhy, vzdálenost, jestli je na Slunci,
výšku Slunce u pozorovatele a nejbližší přelet ISS nad 10°. Hodiny mezi body
interpolují a ptají se jednou za minutu.

**Odkud jsou dráhy.** Z [CelesTraku](https://celestrak.org/NORAD/elements/),
skupiny `stations`, `visual`, `weather`, `gnss`, `amateur` a `starlink`, ve
formátu OMM (JSON). TLE se nepoužívá: katalogová čísla nad 99999 se do něj
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
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/satellites --shell /sbin/nologin satellites \
    && sudo install -d -o root -g satellites -m 750 /opt/satellites'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/satellites/serve.py infra/satellites/requirements.txt infra/satellites/satellites-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py requirements.txt satellites-web.service /opt/satellites/ \
    && rm serve.py requirements.txt satellites-web.service \
    && sudo /usr/bin/python3.11 -m venv /opt/satellites/.venv \
    && sudo /opt/satellites/.venv/bin/pip install --quiet -r /opt/satellites/requirements.txt'
NEW="$(openssl rand -hex 24)"   # do .env jako SATELLITES_PASSWORD
SSH "$CLOCK_SSH" "echo SATELLITES_HASH=\$(caddy hash-password --plaintext '$NEW') | sudo tee -a /etc/caddy/caddy.env >/dev/null"
SSH "$CLOCK_SSH" 'sudo cp /opt/satellites/satellites-web.service /etc/systemd/system/ && sudo systemctl daemon-reload \
    && sudo systemctl enable --now satellites-web.service && sudo systemctl restart caddy'
```

Caddyfile s blokem `/satellites.json` se nasazuje jako obvykle (viz „Nasazení
změn z repozitáře“), **až po** zapsání `SATELLITES_HASH`. Do hodin se pak opíše
`https://hodiny:$SATELLITES_PASSWORD@$CLOCK_HOST/satellites.json` v záložce
**Družice**. Testy bez sítě běží v repozitáři s libovolným prostředím, které má
`sgp4` a `numpy` (stejné verze jako `requirements.txt`):
`python -m unittest infra/satellites/test_serve.py`.

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
set -a; . ./.env; set +a
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/rain --shell /sbin/nologin rain \
    && sudo install -d -o root -g rain -m 750 /opt/rain'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/rain/serve.py infra/rain/rain-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py rain-web.service /opt/rain/ \
    && rm serve.py rain-web.service'
NEW="$(openssl rand -hex 24)"   # do .env jako RAIN_PASSWORD
SSH "$CLOCK_SSH" "echo RAIN_HASH=\$(caddy hash-password --plaintext '$NEW') | sudo tee -a /etc/caddy/caddy.env >/dev/null"
SSH "$CLOCK_SSH" 'sudo cp /opt/rain/rain-web.service /etc/systemd/system/ && sudo systemctl daemon-reload \
    && sudo systemctl enable --now rain-web.service && sudo systemctl restart caddy'
```

Caddyfile s blokem `/rain.json` se nasazuje jako obvykle (viz „Nasazení změn
z repozitáře“), **až po** zapsání `RAIN_HASH`. Testy bez sítě:
`python3 -m unittest infra/rain/test_serve.py`.

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
- **Obědy** (`meals`) pod rozvrhem místo úkolů: dnes a zítra, u každého dne
  školní jídelna a školka, jeden řádek `{"when":"DNES","who":"ZŠ","text":…}`.
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
  Den bez jídla (víkend, svátek bez záznamu) se vynechá.
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
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/school --shell /sbin/nologin school \
    && sudo install -d -o root -g school -m 750 /opt/school'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/school/feed.py infra/school/serve.py infra/school/school-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 feed.py serve.py school-web.service /opt/school/ \
    && rm feed.py serve.py school-web.service'
# Přihlášení: vzor je infra/school/school.env.example.
SSH -t "$CLOCK_SSH" 'sudo install -o root -g root -m 600 /dev/null /opt/school/school.env && sudo vi /opt/school/school.env'
# Ověření, že účet vidí dítě a API vrací to, co feed.py čte (vypisuje skutečná data):
SSH -t "$CLOCK_SSH" 'sudo systemd-run --pty --quiet --uid=school -p EnvironmentFile=/opt/school/school.env /usr/bin/python3.11 /opt/school/serve.py --probe'
NEW="$(openssl rand -hex 24)"   # do .env jako SCHOOL_PASSWORD
SSH "$CLOCK_SSH" "echo SCHOOL_HASH=\$(caddy hash-password --plaintext '$NEW') | sudo tee -a /etc/caddy/caddy.env >/dev/null"
SSH "$CLOCK_SSH" 'sudo cp /opt/school/school-web.service /etc/systemd/system/ && sudo systemctl daemon-reload \
    && sudo systemctl enable --now school-web.service && sudo systemctl restart caddy'
```

Caddyfile s blokem `/school.json` se nasazuje jako obvykle (viz „Nasazení
změn z repozitáře“), a to **až po** zapsání `SCHOOL_HASH`: bez proměnné by
Caddy při restartu spadl. Do hodin se pak opíše
`https://hodiny:$SCHOOL_PASSWORD@$CLOCK_HOST/school.json` v záložce **Škola**.

## Nasazení změn z repozitáře

Soubory se mění tady a teprve pak jdou na server; opačné pořadí je přesně to,
co `--deep` ohlásí jako rozjeté. Po commitu:

```sh
set -a; . ./.env; set +a          # načte CLOCK_HOST, CLOCK_SSH, CLOCK_SSH_KEY
SSH() { ssh -i "$CLOCK_SSH_KEY" -o PubkeyAcceptedAlgorithms=+ssh-rsa "$@"; }
SSH "$CLOCK_SSH" 'mkdir -p news-deploy'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/news/generate.py infra/news/serve.py infra/news/locations.py \
    infra/news/news.service infra/news/news.timer infra/news/news-web.service \
    "$CLOCK_SSH:news-deploy/"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 news-deploy/* /opt/news/ && rm -r news-deploy \
    && sudo cp /opt/news/news*.service /opt/news/news.timer \
    /etc/systemd/system/ && sudo systemctl daemon-reload \
    && sudo systemctl restart news-web.service && sudo systemctl start news.service'
```

Soubory jdou nejdřív do domovského adresáře `opc` a na místo je dá `sudo
install` jako rootovy: adresáře v `/opt` od 13. 9. 2026 `opc` nepatří (viz
„Zabezpečení stroje“), takže přímé `scp` do nich skončí na právech. Agenda se
nasazuje stejně, do `/opt/agenda/`.

Letadla jsou první služba bez `.venv`: vystačí si se standardní knihovnou.
Jednotka volá **`/usr/bin/python3.11`**, ne `python3` — ten je na Oracle Linuxu 8
pořád ještě 3.6 a ta neumí ani `from __future__ import annotations`. Poprvé je
potřeba založit adresář a jednotku povolit:

```sh
SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/planes --shell /sbin/nologin planes \
    && sudo install -d -o root -g planes -m 750 /opt/planes'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/planes/serve.py infra/planes/planes-web.service "$CLOCK_SSH:"
SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py planes-web.service /opt/planes/ \
    && sudo cp /opt/planes/planes-web.service /etc/systemd/system/ \
    && sudo systemctl daemon-reload \
    && sudo systemctl enable --now planes-web.service'
```

`infra/caddy/Caddyfile` drží místo domény `{{DOMAIN}}`, opět aby adresa nebyla
ve veřejném repozitáři. Před nasazením se dosadí a `check-stack.sh` ho po
dosazení i porovnává:

```sh
sed "s/{{DOMAIN}}/$CLOCK_HOST/g" infra/caddy/Caddyfile > /tmp/Caddyfile
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    /tmp/Caddyfile "$CLOCK_SSH:/home/${CLOCK_SSH%%@*}/Caddyfile.new"
SSH "$CLOCK_SSH" 'sudo cp ~/Caddyfile.new /etc/caddy/Caddyfile \
    && sudo systemctl reload caddy'
```

Samostatné `caddy validate` tu schválně není: Caddyfile od zaheslování agendy
obsahuje `{env.AGENDA_HASH}` a v obyčejném shellu ta proměnná není, takže by
validace spadla na prázdném hesle. `systemctl reload` konfiguraci ověří sám
a při chybě skončí nenulově — běžet přitom dál zůstane ta stará.

**Nový hash v `caddy.env` potřebuje `restart`, ne `reload`.** Proměnné z
`EnvironmentFile` čte systemd jen při startu procesu, takže po přidání řádku
`*_HASH` se `reload` sice provede, ale nový `{env.*_HASH}` je pro běžící Caddy
prázdný a celá konfigurace se odmítne:

```
http_basic: account 0: username and password are required
```

Zní to, jako by byl špatně Caddyfile, ale chybí jen heslo v prostředí. Stalo se
to při zavádění srážek 20. 9. 2026. Caddy přitom běží dál se starou konfigurací
— jenže v `/etc/caddy/Caddyfile` už leží ta nová, takže se to musí dorovnat
hned, jinak spadne až příští restart, klidně za měsíc při aktualizaci:

```sh
SSH "$CLOCK_SSH" 'sudo systemctl restart caddy'
```

Proto mají návody na zavedení nové služby na konci `restart caddy`, kdežto
běžná změna Caddyfile bez nového hesla si vystačí s `reload`.

Jednotky (`news.service`, `news.timer`, `news-web.service`) leží na serveru
ve dvou místech: kopie v `/opt/news/` je ta, kterou porovnává `--deep`, běhová
je v `/etc/systemd/system/`. Po Caddyfile stačí `sudo systemctl reload caddy`.
Nakonec `tools/check-stack.sh --deep` — musí projít beze zbytku.

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

Kód a `.venv` patří rootovi, takže je služba nezmění. `news.env`
a `agenda.env` jsou rootovy (600): systemd je načte do prostředí ještě před
přepnutím na uživatele, ale jako soubor je služba nepřečte. Python kvůli tomu
nemůže zapsat `__pycache__` vedle kódu, což nevadí. `/opt/watch` (jiný
projekt) má uživatele `watch` odjakživa.

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

**Zbývá (nepovinné):** SSH klíč je RSA (odtud `+ssh-rsa` ve všech příkazech).
Nový ed25519 klíč přidat do `authorized_keys`, přepnout `CLOCK_SSH_KEY`
v `.env` a starý klíč z OCI konzole pak odebrat.

## Zálohy dat

Tenhle adresář umí obnovit **software**. Neumí obnovit **data** — a několik
věcí na serveru nemá kopii nikde jinde:

| Co | Kde | Proč to jinde není |
|---|---|---|
| Konfigurace Home Assistanta | `/path/to/your/config` (bind-mount kontejneru) | `configuration.yaml`, `secrets.yaml`, automatizace a hlavně `.storage` — registr entit, uživatelé, tokeny a integrace naklikané v UI. Bez `.storage` je obnovené HA prázdné, i když YAML sedí. |
| Historie senzorů | `home-assistant_v2.db` | Grafy a statistiky. Šablonové senzory na ní nestojí. |
| Stav hlídačů | `/opt/watch/state.db`, `/opt/ou-watch/state.db` | Co už hlídač viděl. Bez toho přijde po restartu buď záplava „novinek“, nebo se dávka tiše ztratí. |
| Hesla a klíče | `caddy.env`, `news.env`, `agenda.env`, `key.json`, `school.env`, obě `watch.env` | Do veřejného repozitáře nepatří. Většina se dá vyrobit znovu, `key.json` se vydá nový ve stejném projektu Google Cloudu (sdílení kalendářů zůstává). |

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

Archivy se **nešifrují**, a je to vědomé rozhodnutí. U noční úlohy bez obsluhy
by heslo muselo ležet na serveru vedle toho, co šifruje, takže by nekrylo nic;
smysl by dávalo jedině šifrování veřejným klíčem, kde server dostane jen tu
půlku, kterou se zašifrovat dá. Za to se platí tím, že ztráta soukromého klíče
znamená ztrátu všech archivů najednou — a to je horší riziko než to, které by
řešilo, dokud se zálohy nedostanou někam mimo Mac a server.

**Až je někam pošleš** — Object Storage, cizí disk, jiný stroj —, tahle úvaha
přestane platit a šifrování je potřeba doplnit. Totéž platí pro každou další
zálohovací službu na Macu (Backblaze a spol.): vyloučit `~/waveshare-zalohy`
stejně jako u Time Machine, jinak tajemství odtečou tam.

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
(`CLOCK_SSH_KEY=$HOME/.ssh/majnr-oracle.key`) — ten adresář TCC nehlídá.
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
tools/check-stack.sh          # zvenčí: kanál, HA, certifikát — bez SSH
tools/check-stack.sh --deep   # + systemd jednotky a shoda tohohle adresáře
```

Nahrazuje pamatování si diagnostického postupu. Když hodiny neukazují zprávy,
skript řekne, jestli je vadný kanál, generátor, proxy nebo certifikát.

## Obnova serveru

1. Nový stroj, otevřít 80/443 v OCI i ve `firewalld`. Uživatelé služeb:
   `for u in news agenda planes settings school satellites; do sudo useradd --system
   --no-create-home --home-dir /opt/$u --shell /sbin/nologin $u; done`
   a vlastnictví podle tabulky v „Zabezpečení stroje“.
2. Caddy: binárku do `/usr/bin/caddy`, `caddy/Caddyfile` do `/etc/caddy/`
   (s dosazenou doménou), `caddy/caddy.service` do `/etc/systemd/system/`,
   uživatel `caddy`, `HOME=/var/lib/caddy`. `caddy.env.example` →
   `/etc/caddy/caddy.env` s hashem hesla k agendě (práva 600) — bez něj se
   Caddy nespustí, viz „Heslo k agendě".
3. Zprávy: `news/*.py` do `/opt/news/`, `.venv` s `google-genai`, `feedparser`
   a `pydantic`, `news.env.example` → `news.env` s klíčem (práva 600),
   jednotky do `/etc/systemd/system/`, `systemctl enable --now news-web.service
   news.timer`.
4. Agenda: `agenda/*.py` (včetně `feed.py`) do `/opt/agenda/` (práva adresáře 700), `.venv`
   s `google-auth` a `requests` postavené **`/usr/bin/python3.11`**, klíč
   servisního účtu do `key.json` (práva 600), `agenda.env.example` →
   `agenda.env` s ID kalendářů (práva 600), jednotky do
   `/etc/systemd/system/`, `systemctl enable --now agenda-web.service
   agenda.timer`.
5. Zálohy nastavení (nepovinné): `settings/*` do `/opt/settings/`, adresáře
   `/opt/settings` a `/opt/settings/data` s právy 700, `SETTINGS_HASH` do
   `/etc/caddy/caddy.env`, jednotku do `/etc/systemd/system/`, `systemctl
   enable --now settings-web.service`. Samotné zálohy v `data/` se dají
   kdykoli znovu nahrát z hodin.
6. Rozvrh (nepovinné): viz „Rozvrh a úkoly ze Školy OnLine“ — `school/*.py`
   a jednotka do `/opt/school/`, `school.env` s přihlášením, `SCHOOL_HASH`
   do `/etc/caddy/caddy.env`.
7. Družice (nepovinné): viz „Družice“ — `.venv` z `satellites/requirements.txt`,
   `SATELLITES_HASH` do `/etc/caddy/caddy.env`. Dráhy se stáhnou samy při
   prvním dotazu hodin.
8. Zálohy: `backup/backup.sh` do `/opt/backup/` (adresář 700, skript 700
   vlastník root), `backup.env.example` → `/opt/backup/backup.env` s cestou
   ke konfiguraci HA (práva 600), jednotky do `/etc/systemd/system/`,
   `restorecon -v /opt/backup/backup.sh`, `systemctl enable --now
   backup.timer`. Data z posledního staženého archivu vrať podle oddílu
   „Zálohy dat“ — hesla a `key.json` z něj ušetří většinu kroků výše.
   Na Macu pak `tools/install-pull-backup-agent.sh`, ať se stahuje dál samo.
9. `tools/check-stack.sh --deep`.

**Python na tom stroji:** `python3` je 3.6.8, který nemá `zoneinfo` a tiše
nainstaluje roky staré verze knihoven. Obě `.venv` se proto stavějí výslovně
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
