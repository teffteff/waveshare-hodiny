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
| Caddy (HTTPS proxy) | 80, 443 | `/etc/caddy/Caddyfile`, `/etc/systemd/system/caddy.service` | `caddy/` |
| Generátor zpráv | — | `/opt/news/generate.py`, `news.service` + `news.timer` | `news/` |
| Server se zprávami | 8088 | `/opt/news/serve.py`, `news-web.service` | `news/` |
| Generátor agendy | — | `/opt/agenda/generate.py`, `agenda.service` + `agenda.timer` | `agenda/` |
| Server s agendou | 8089 | `/opt/agenda/serve.py`, `agenda-web.service` | `agenda/` |
| Home Assistant | 8123 | Docker, `--network=host`, config bind-mount | — |
| Ostatní | 25565 | Minecraft, go2rtc — s hodinami nesouvisí | — |

Generátor jede na **Google Gemini**, ne na Claude: `.venv` s `google-genai`,
klíč `GEMINI_API_KEY` v `/opt/news/news.env` (práva 600). Timer pouští výběr
8× denně mezi 06:05 a 20:05. Když cokoli selže, skript skončí nenulově a
**nechá předchozí soubor být** — na hodinách zůstanou starší zprávy místo
prázdna. Stará stopa v repu: `news/news.env.example` je oproti serveru
opravená (server má pořád původní variantu s `ANTHROPIC_API_KEY` z doby, kdy
se čekalo, že generátor pojede na Claude). Kontrola shody tenhle soubor
schválně přeskakuje.

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
https://$CLOCK_HOST/agenda.json  agenda z kalendáře
https://$CLOCK_HOST              Home Assistant
```

Do hodin se nezadávají ručně: obě adresy jsou v kořenovém `.env`
(`NEWS_URL`, `HOME_ASSISTANT_URL`), `tools/generate_secrets.py` z nich udělá
`WaveshareHodiny/local/secrets.h` a vývojový build je předvyplní do prázdné
konfigurace. Přepisuje se **jen prázdné pole**, takže ručně zadanou adresu
build nepřemaže — na zařízení, které má v NVS ještě starou `http://` adresu,
je proto potřeba ji jednou přepsat ve webovém rozhraní.

Firmware kvůli HTTPS měnit netřeba: RSS používá celý CA bundle, Home Assistant
připnutý ISRG Root X1 a Let's Encrypt se do něj řetězí.

## Porty

Musí být otevřené **na dvou místech** — v OCI (security list dané VCN) i ve
`firewalld` na stroji. Když ACME hlásí „Timeout during connect (likely firewall
problem)“, jedno z těch dvou je zavřené.

- 80, 443 — Caddy a obnova certifikátu. **Bez nich certifikát tiše vyprší.**
- 8088 — přímý přístup ke kanálu, dnes už jen záloha
- 8089 — přímý přístup k agendě, taky jen záloha
- 8123 — přímý přístup k HA, taky jen záloha

Certifikát vydává Let's Encrypt přes tls-alpn-01, obnovuje ho Caddy sám.
Neúspěšné pokusy jsou limitované (~5/h), takže **restartovat Caddy kvůli
opakování nemá smysl** — sám si počká.

## Agenda z Google Kalendáře

`generate.py` čte kalendáře přes **servisní účet** Google Cloudu, ne přes
uživatelský OAuth, a každých 15 minut zapíše `agenda.json` do
`/opt/agenda/www/`. Odpověď má kolem kilobajtu a nese hotové řetězce: popisek
dne, čas, titulek a index kalendáře. Časová zóna, expanze opakovaných událostí
i skládání popisků se dělají tady, aby ve firmwaru nezůstala žádná datumová
aritmetika.

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
jako barvu, kterou událost odliší. Klíč účtu leží v `/opt/agenda/key.json`,
práva 600, v adresáři s právy 700. Ani jeden soubor nepatří do repozitáře.

Účet i klíč vznikly v projektu `gen-lang-client-…`, tedy v tom, který Googlu
založilo AI Studio pro klíč Gemini. Je to schválně: generátor zpráv i agenda
sdílejí jeden projekt.

Prázdná agenda je **legitimní stav** — kalendář prostě nemusí nic mít. Proto ji
`check-stack.sh` hlásí jako `warn`, ne jako `FAIL`, na rozdíl od prázdného
kanálu se zprávami, kde prázdno vždycky znamená rozbitý běh.

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
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    infra/news/generate.py infra/news/serve.py "$CLOCK_SSH:/opt/news/"
$SSH "$CLOCK_SSH" 'sudo cp /opt/news/news*.service /opt/news/news.timer \
    /etc/systemd/system/ && sudo systemctl daemon-reload \
    && sudo systemctl restart news-web.service && sudo systemctl start news.service'

`infra/caddy/Caddyfile` drží místo domény `{{DOMAIN}}`, opět aby adresa nebyla
ve veřejném repozitáři. Před nasazením se dosadí a `check-stack.sh` ho po
dosazení i porovnává:

```sh
sed "s/{{DOMAIN}}/$CLOCK_HOST/g" infra/caddy/Caddyfile > /tmp/Caddyfile
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i "$CLOCK_SSH_KEY" \
    /tmp/Caddyfile "$CLOCK_SSH:/home/${CLOCK_SSH%%@*}/Caddyfile.new"
$SSH "$CLOCK_SSH" 'sudo cp ~/Caddyfile.new /etc/caddy/Caddyfile \
    && sudo caddy validate --config /etc/caddy/Caddyfile \
    && sudo systemctl reload caddy'
```
```

Jednotky (`news.service`, `news.timer`, `news-web.service`) leží na serveru
ve dvou místech: kopie v `/opt/news/` je ta, kterou porovnává `--deep`, běhová
je v `/etc/systemd/system/`. Po Caddyfile stačí `sudo systemctl reload caddy`.
Nakonec `tools/check-stack.sh --deep` — musí projít beze zbytku.

## Kontrola

```sh
tools/check-stack.sh          # zvenčí: kanál, HA, certifikát — bez SSH
tools/check-stack.sh --deep   # + systemd jednotky a shoda tohohle adresáře
```

Nahrazuje pamatování si diagnostického postupu. Když hodiny neukazují zprávy,
skript řekne, jestli je vadný kanál, generátor, proxy nebo certifikát.

## Obnova serveru

1. Nový stroj, otevřít 80/443 v OCI i ve `firewalld`.
2. Caddy: binárku do `/usr/bin/caddy`, `caddy/Caddyfile` do `/etc/caddy/`,
   `caddy/caddy.service` do `/etc/systemd/system/`, uživatel `caddy`,
   `HOME=/var/lib/caddy`.
3. Zprávy: `news/*.py` do `/opt/news/`, `.venv` s `google-genai`, `feedparser`
   a `pydantic`, `news.env.example` → `news.env` s klíčem (práva 600),
   jednotky do `/etc/systemd/system/`, `systemctl enable --now news-web.service
   news.timer`.
4. Agenda: `agenda/*.py` do `/opt/agenda/` (práva adresáře 700), `.venv`
   s `google-auth` a `requests` postavené **`/usr/bin/python3.11`**, klíč
   servisního účtu do `key.json` (práva 600), `agenda.env.example` →
   `agenda.env` s ID kalendářů (práva 600), jednotky do
   `/etc/systemd/system/`, `systemctl enable --now agenda-web.service
   agenda.timer`.
5. `tools/check-stack.sh --deep`.

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
