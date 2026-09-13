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
takže se dají příkazy kopírovat po `set -a; . .env; set +a`.

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
| Zálohy nastavení | 8092, jen loopback | `/opt/settings/serve.py`, `settings-web.service`, data v `/opt/settings/data/` | `settings/` |
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
8090 a 8092 mezi ně nepatří**: servery se zprávami, agendou, letadly
a zálohami poslouchají jen na `127.0.0.1`, protože jinak by šlo heslo
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

**Záloha může nést token Home Assistantu a hash hesla webu**, proto:

- stojí za `basic_auth` v Caddy s **vlastním heslem** (`SETTINGS_HASH`), ne
  s tím k agendě – to zná každé hodiny, které agendu jen čtou,
- server poslouchá jen na `127.0.0.1`, port 8092 se nikde neotevírá,
- název je `[a-z0-9][a-z0-9-]{0,31}`, takže z něj nejde složit cesta,
- soubory mají práva 600 v adresáři 700, zápis jde přes dočasný soubor
  a `os.replace`,
- tělo má strop 64 kB (Caddy i server) a musí to být obálka zálohy hodin,
- záloh je nejvýš 64, aby se disk nedal zaplnit ani s heslem.

Tokeny a hesla se do zálohy dostanou, jen když je majitel při zálohování potvrdí
heslem webového nastavení – bez něj hodiny pošlou zálohu bez nich. Firmware
přijme jen adresu `https://` a přesměrování nenásleduje, aby heslo z adresy
nešlo jinam.

Zavedení (jednou):

```sh
set -a; . .env; set +a
SSH="ssh -i $CLOCK_SSH_KEY -o PubkeyAcceptedAlgorithms=+ssh-rsa"
NEW="$(openssl rand -hex 24)"      # hex: bez : a @ kvůli adrese
HASH="$($SSH "$CLOCK_SSH" "caddy hash-password --plaintext '$NEW'")"
# Hash se do caddy.env PŘIDÁ, AGENDA_HASH musí zůstat.
$SSH "$CLOCK_SSH" "printf 'SETTINGS_HASH=%s\n' '$HASH' | sudo tee -a /etc/caddy/caddy.env >/dev/null && sudo chmod 600 /etc/caddy/caddy.env"
$SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/settings --shell /sbin/nologin settings \
    && sudo install -d -o root -g settings -m 750 /opt/settings \
    && sudo install -d -o settings -g settings -m 700 /opt/settings/data'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/settings/serve.py infra/settings/settings-web.service "$CLOCK_SSH:"
$SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py settings-web.service /opt/settings/ \
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
   set -a; . .env; set +a
   SSH="ssh -i $CLOCK_SSH_KEY -o PubkeyAcceptedAlgorithms=+ssh-rsa"
   HASH="$($SSH "$CLOCK_SSH" "caddy hash-password --plaintext '$PLANES_PASSWORD'")"
   # Hash se do caddy.env PŘIDÁ, ostatní řádky musí zůstat.
   $SSH "$CLOCK_SSH" "printf 'PLANES_HASH=%s\n' '$HASH' | sudo tee -a /etc/caddy/caddy.env >/dev/null && sudo chmod 600 /etc/caddy/caddy.env"
   $SSH "$CLOCK_SSH" 'sudo systemctl restart caddy'
   ```

3. Nový Caddyfile podle „Nasazení změn z repozitáře“.
4. `tools/check-stack.sh --deep`: bez hesla 401, s heslem seznam letadel.

Výměna hesla je stejná jako u agendy, jen se `sed` nahradí řádek `PLANES_HASH`
a nová adresa se opíše do hodin.

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

## Nasazení změn z repozitáře

Soubory se mění tady a teprve pak jdou na server; opačné pořadí je přesně to,
co `--deep` ohlásí jako rozjeté. Po commitu:

```sh
set -a; . .env; set +a          # načte CLOCK_HOST, CLOCK_SSH, CLOCK_SSH_KEY
SSH="ssh -i $CLOCK_SSH_KEY -o PubkeyAcceptedAlgorithms=+ssh-rsa"
$SSH "$CLOCK_SSH" 'mkdir -p news-deploy'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/news/generate.py infra/news/serve.py infra/news/locations.py \
    infra/news/news.service infra/news/news.timer infra/news/news-web.service \
    "$CLOCK_SSH:news-deploy/"
$SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 news-deploy/* /opt/news/ && rm -r news-deploy \
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
$SSH "$CLOCK_SSH" 'sudo useradd --system --no-create-home --home-dir /opt/planes --shell /sbin/nologin planes \
    && sudo install -d -o root -g planes -m 750 /opt/planes'
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/planes/serve.py infra/planes/planes-web.service "$CLOCK_SSH:"
$SSH "$CLOCK_SSH" 'sudo install -o root -g root -m 644 serve.py planes-web.service /opt/planes/ \
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
$SSH "$CLOCK_SSH" 'sudo cp ~/Caddyfile.new /etc/caddy/Caddyfile \
    && sudo systemctl reload caddy'
```

Samostatné `caddy validate` tu schválně není: Caddyfile od zaheslování agendy
obsahuje `{env.AGENDA_HASH}` a v obyčejném shellu ta proměnná není, takže by
validace spadla na prázdném hesle. `systemctl reload` konfiguraci ověří sám
a při chybě skončí nenulově — běžet přitom dál zůstane ta stará.

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

**Zbývá udělat ručně:**

- Home Assistant běží s `--privileged` a `--network=host`. Průnik do HA,
  který je jako jediná aplikace vystavený do internetu, tak znamená root na
  celém stroji. Kontejner nemá namapované žádné zařízení, takže privileged
  nejspíš nepotřebuje; znovu vytvořit bez něj ho musí člověk u terminálu.
- Účet vlastníka HA nemá dvoufaktor (Profil → Zabezpečení → TOTP).
- SSH klíč je RSA (odtud `+ssh-rsa` ve všech příkazech). Nový ed25519 klíč
  přidat do `authorized_keys`, přepnout `CLOCK_SSH_KEY` v `.env` a starý
  klíč z OCI konzole pak odebrat.

## Kontrola

```sh
tools/check-stack.sh          # zvenčí: kanál, HA, certifikát — bez SSH
tools/check-stack.sh --deep   # + systemd jednotky a shoda tohohle adresáře
```

Nahrazuje pamatování si diagnostického postupu. Když hodiny neukazují zprávy,
skript řekne, jestli je vadný kanál, generátor, proxy nebo certifikát.

## Obnova serveru

1. Nový stroj, otevřít 80/443 v OCI i ve `firewalld`. Uživatelé služeb:
   `for u in news agenda planes settings; do sudo useradd --system
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
6. `tools/check-stack.sh --deep`.

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
zkouší dvakrát a pak se jde na záložní.
