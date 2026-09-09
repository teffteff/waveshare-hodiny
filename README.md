# Waveshare Hodiny

🇬🇧 **[English documentation](README.en.md)**

Český informační dashboard pro kulatý dotykový displej
[Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)
s rozlišením 480 × 480 px. Zobrazuje čas, datum, počasí, teploty, další
měřené hodnoty a srážkový radar ČHMÚ. Jako zdroj hodnot lze použít Open-Meteo
bez účtu, volitelně doplněné vlastními čidly TMEP.cz, nebo Home Assistant.
Vzhled, zdroje dat, poloha, radar, jas, animace i aktualizace se nastavují
z webového rozhraní bez úpravy zdrojového kódu.

<p align="center">
  <a href="https://teffteff.github.io/waveshare-hodiny/">
    <img src="https://img.shields.io/badge/Nainstalovat_firmware_z_prohl%C3%AD%C5%BEe%C4%8De-00BBD4?style=for-the-badge&amp;logo=googlechrome&amp;logoColor=white" alt="Nainstalovat firmware z prohlížeče" height="46">
  </a>
</p>

<p align="center">
  <strong>Jednoduchá instalace přes USB bez stahování souborů.</strong><br>
  Otevřete instalační stránku v desktopovém Chromu nebo Edge, připojte displej a pokračujte podle průvodce.
</p>

---

<p align="center">
  <img src="screenshots/dashboard.png" alt="Hlavní obrazovka Waveshare Hodiny v denním režimu" width="46%">
  <img src="screenshots/dashboard-analog.png" alt="Analogový ciferník Waveshare Hodiny v denním režimu" width="46%">
</p>

<p align="center">
  <img src="screenshots/dashboard-values.png" alt="Ciferník HODNOTY s mřížkou osmi hodnot a devátou pod ní" width="30%">
  <img src="screenshots/dashboard-night.png" alt="Hlavní obrazovka Waveshare Hodiny v červeném nočním režimu" width="30%">
  <img src="screenshots/dashboard-analog-night.png" alt="Analogový ciferník Waveshare Hodiny v červeném nočním režimu" width="30%">
</p>

<p align="center">
  <img src="screenshots/dashboard-radar.png" alt="Meteoradar ČHMÚ na displeji Waveshare Hodiny" width="30%">
</p>

V denním režimu mají jednotlivé hodnoty vlastní barvy. Volitelný červený
noční vzhled sjednotí dashboard i meteoradar do odstínů červené a sníží jas,
aby displej v noci nerušil. Vedle klasického digitálního rozložení lze zvolit
také analogový ciferník s vlastním barevným tónem a volitelnými akcenty na
pozicích 12, 3, 6 a 9 hodin, nebo ciferník HODNOTY: malý čas a datum nahoře
a pod nimi mřížka až osmi nezávislých hodnot s devátou na středu pod nimi.

## Co firmware umí

- digitální hodiny s fonty Barlow, Liberation Sans, LCD DSEG nebo Doto,
  analogový ciferník s nastavitelným tónem a volitelnými hlavními akcenty,
  nebo ciferník HODNOTY s mřížkou až osmi hodnot a devátou pod nimi,
- české nebo anglické datum v několika formátech a volitelný vteřinový prstenec,
- jmeniny pro dnešní den pod datem, počítané přímo na zařízení bez sítě,
- synchronizaci času přes NTP a české časové pásmo včetně letního času,
- dvě univerzální horní hodnoty s vlastním názvem, jednotkou, přesností,
  ikonou a plynulou barevnou škálou,
- animované i statické ikony počasí založené na Meteocons,
- srážkový radar ČHMÚ s mapou České republiky, městy a 1 až 15 snímky,
- druhý zdroj srážek RainViewer s obrysy evropských států a městy, takže radar
  funguje i mimo Českou republiku,
- volitelnou stupnici intenzity srážek v dBZ a mm/h podle zvoleného zdroje,
- rozsahy 25, 50, 100 a 200 km nebo celou ČR ovládané přetažením prstu,
- červenou noční paletu radaru se zachováním rozlišení intenzity srážek,
- volitelné automatické střídání hodin, radaru, zpráv, předpovědi, letadel
  a agendy se samostatnou dobou zobrazení a vlastním pořadím obrazovek,
- obrazovku se zprávami z libovolného kanálu RSS nebo Atom,
- obrazovku s agendou z Google Kalendáře, sloučenou z několika kalendářů
  a obarvenou podle toho, ze kterého z nich událost je,
- obrazovku s hodinovou a denní předpovědí z Open-Meteo, volitelně s kvalitou
  ovzduší, PM2.5 a pylem trav — a bez ní s devíti hodinami místo šesti,
- radar letadel z veřejného API adsb.fi: mapa okolí s letadly obarvenými podle
  výšky, dosahy 10 až 100 km, filtr výšky, upozornění na nouzový squawk,
  hlídaný let a detail letadla i s trasou letu z adsb.lol,
- dvě další měřené veličiny, například CO₂, VOC, vlhkost, tlak nebo baterii,
- devět nezávislých hodnot ciferníku HODNOTY, každou s vlastním názvem,
  entitou Home Assistantu, jednotkou, přesností a barevnou škálou,
- vlastní čidla TMEP.cz jako volitelný doplněk hodnot Open-Meteo,
- vlastní jednotky, počet desetinných míst a plynulé barevné škály,
- denní a noční jas s ručním přepínáním nebo automatikou podle Open-Meteo či entity slunce,
- tři efekty vteřin: klasické tečky, plynulou čáru a kometu,
- webovou konfiguraci s volitelným heslem, export a import zálohy a bezpečný restart,
- samostatnou živou diagnostiku hardwaru, paměti, sítě, Home Assistantu a radaru,
- prvotní nastavení Wi-Fi přes Improv Serial,
- A/B OTA aktualizace se zachováním Wi-Fi a konfigurace,
- ovládací API pro Home Assistant chráněné náhodným secretem,
- základní nastavení také přímo na dotykovém displeji.

## Potřebný hardware

Firmware je určený výhradně pro **Waveshare ESP32-S3-Touch-LCD-2.1** s
480 × 480 px displejem a 16MiB flash. Konfigurace pinů, displeje ST7701,
dotyku CST820, PSRAM a partition table odpovídá této konkrétní desce.

Desku můžete zakoupit u českých prodejců:

<p align="center">
  <a href="https://pajenicko.cz/waveshare-esp32-s3-touch-lcd-2.1-s-kulatym-ips-lcd-dotykovym-displejem"><img src="docs/assets/retailers/pajenicko.png" alt="Koupit podporovanou desku na Pájeníčko.cz" height="60"></a>&nbsp;&nbsp;&nbsp;&nbsp;
  <a href="https://www.laskakit.cz/waveshare-esp32-s3-round-2-1--480--480-ips-touch-wifi-modul/"><img src="docs/assets/retailers/laskakit.png" alt="Koupit podporovanou desku na LaskaKit" height="60"></a>
</p>

Nepoužívejte tento binární obraz na jiném modelu jen proto, že také obsahuje
ESP32-S3. Odlišný pinout nebo flash layout může zabránit startu zařízení.

## Instalace pro běžného uživatele

### Instalace z prohlížeče

Veřejná [instalační stránka na GitHub Pages](https://teffteff.github.io/waveshare-hodiny/)
umožňuje nahrát stabilní release přímo z desktopového Chromu nebo Edge přes
USB. Instalační tlačítko se zpřístupní, jakmile je na GitHubu dostupný veřejný
stabilní release se zkontrolovaným čtyřdílným factory balíčkem.

Do té doby lze použít release balíček s manifestem v
[ESP Web Tools](https://web.esphome.io/) nebo firmware sestavit ze zdrojů
podle kapitoly [Sestavení ze zdrojů](#sestavení-ze-zdrojů). Factory instalace
vyžaduje všechny části a přesné offsety uvedené v release `manifest.json`;
samostatný aplikační `.ota.bin` není factory obraz.

### Nastavení Wi-Fi

Veřejný release neobsahuje přednastavené Wi-Fi údaje. Po instalaci připoj
zařízení jedním z jeho USB-C konektorů a použij Improv Serial v instalační
stránce. Zadané SSID a heslo se uloží do NVS a po restartu zůstanou zachované.

Deska má USB–UART konektor přes CH343P a nativní USB konektor ESP32-S3.
Produkční firmware obsluhuje Improv Serial na obou konektorech.

## První spuštění

1. Nainstaluj firmware a nastav Wi-Fi přes Improv Serial.
2. Počkej na připojení; na displeji se zobrazí IP adresa a stavové ikony.
3. Otevři `http://waveshare-hodiny.local/`. Pokud mDNS v síti nefunguje,
   použij IP adresu z nastavení na displeji.
4. V záložce **Zdroj a poloha** vyber Open-Meteo s TMEP.cz nebo Home Assistant
   a vyhledej město. Poloha je společná pro počasí Open-Meteo i meteoradar.
5. Při použití Home Assistantu zadej jeho adresu a long-lived access token a
   tlačítkem **Otestovat připojení** ověř spojení.
6. Uprav vzhled, radar a jas a zvol **Uložit změny**.

Nová konfigurace používá Open-Meteo, polohu Brno a pohled meteoradaru na celou
Českou republiku. Home Assistant není pro základní provoz povinný.

## Zdroje dat

### Open-Meteo

Open-Meteo je výchozí zdroj a nevyžaduje účet ani token. Poskytuje aktuální
počasí a čtyři konfigurovatelné hodnoty. Vybrané město a jeho GPS souřadnice
současně určují střed lokálních pohledů meteoradaru ČHMÚ. U každé ze čtyř
pozic lze samostatně nastavit 0 až 2 desetinná místa; stejné nastavení platí
i při výběru hodnoty TMEP.cz.

### TMEP.cz jako doplněk Open-Meteo

K režimu Open-Meteo lze přidat vlastní čidla z TMEP.cz. Vlož celou URL ze sekce
**Rozšířený JSON – se všemi čidly**, zvol **Ověřit a načíst čidla** a hodnoty až
32 čidel se přidají přímo do stejných čtyř výběrů pod skupinu TMEP.cz. Firmware
používá jednotku vrácenou exportem, takže podporuje i vlastní veličiny.

Je-li vybraná alespoň jedna hodnota TMEP, celý export se načítá jedním HTTPS
požadavkem každou minutu. Bez vybrané hodnoty se katalog načte jednou po startu
a dál se pravidelně neobnovuje. Otevření webové konfigurace nejprve zobrazí
uložený katalog a nejvýše jednou za načtení stránky jej aktualizuje přímo
z TMEP.cz. Open-Meteo se nezávisle obnovuje jednou za 10 minut.

Firmware si z vložené URL bezpečně vybere ID a exportní klíč a požadavek vždy
skládá s `extended=1&all=1`. Citlivé údaje zůstávají uložené v zařízení a API
ani záloha je nevracejí. Volbou **Odebrat TMEP.cz** se smaže URL, katalog,
diagnostický stav i přiřazení TMEP; dotčené pozice se vrátí na výchozí hodnoty
Open-Meteo.

Příklad exportní URL:
`https://tmep.cz/vystup-json.php?id=11746&export_key=XXXXXXXXsd&extended=1&all=1`

### Home Assistant

Firmware čte jednotlivé entity přes REST API Home Assistantu. Nepotřebuje
MQTT, vlastní integraci ani administrátorský účet.

### Vytvoření tokenu

V Home Assistantu otevři svůj uživatelský profil, sekci **Long-lived access
tokens**, vytvoř nový token pro hodiny a vlož jej do webové konfigurace.
Použij účet pouze s oprávněními, která zařízení skutečně potřebuje.

Token se po uložení už do webové stránky neposílá a nelze jej z ní přečíst;
lze jej pouze nahradit. Při testu se uložený token znovu použije jen pro přesně
stejnou uloženou adresu Home Assistantu. Pokud adresu změníš, musíš zadat také
nový token.

Firmware podporuje lokální HTTP i HTTPS servery s vlastním nebo neplatným
certifikátem. U HTTPS spojení s Home Assistantem proto v současnosti neověřuje
certifikát serveru. Tato volba usnadňuje domácí instalace, ale nechrání token
před aktivním útočníkem v síti. Používej firmware pouze v důvěryhodné LAN.

### Doporučené entity

| Údaj | Příklad entity | Poznámka |
| --- | --- | --- |
| Počasí | `weather.domov` | Textový stav HA nebo podporovaný číselný kód |
| Slunce | `sun.sun` | Řídí automatický denní/noční režim |
| Hodnota vlevo | `sensor.venkovni_teplota` | Teplota, CO₂, PM, tlak nebo jiný číselný senzor |
| Hodnota vpravo | `sensor.obyvak_co2` | Teplota, CO₂, PM, tlak nebo jiný číselný senzor |
| Hodnota A/B | `sensor.obyvak_co2` | CO₂, VOC, PM, vlhkost, tlak a další |

ID entit se zadávají ručně. Nedostupná nebo neplatná hodnota se na displeji
zobrazí jako `--`.

## Webová konfigurace

<p align="center">
  <img src="screenshots/web-configuration.png" alt="Webová konfigurace Home Assistantu a entit" width="920">
</p>

Web umožňuje nastavit:

- jazyk zařízení; dokud není uložená volba, displej používá češtinu a při
  prvním otevření webu se uloží čeština pro prohlížeče `cs`/`sk`, jinak
  angličtina; další návštěvy už respektují uložené nastavení zařízení a jazyk
  lze kdykoli přepnout vlajkami v pevném horním pruhu webu,
- zdroj dat Open-Meteo nebo Home Assistant a společnou polohu zařízení,
- Home Assistant URL, token, entitu počasí a entitu slunce,
- levou a pravou horní hodnotu včetně typu, názvu, jednotky, přesnosti, ikony
  a barevné škály,
- styl animovaných ikon `Monochrome`, `Flat` nebo `Line`,
- meteoradar ČHMÚ s obrysem ČR, městy, pohledy 25, 50, 100, 200 km nebo celá ČR a volbou 1 až 15 snímků,
- radar letadel z adsb.fi: dosah 10 až 100 km, interval dotazů, azimut nahoře
  na displeji, filtr výšky, hlídaný let, upozornění na nouzový squawk
  a jednotky detailu,
- měřené hodnoty A a B, jednotky, přesnost a barevné škály,
- devět hodnot ciferníku HODNOTY, každou zvlášť zapínatelnou, s vlastní entitou,
  názvem, jednotkou, přesností a barevnou škálou; sekce se zobrazí jen se
  zdrojem dat Home Assistant, protože sloty čtou entity,
- pořadí devíti hodnot přetažením za úchyt v záhlaví nebo tlačítky ↑ a ↓;
  pořadí odpovídá mřížce na displeji a stěhuje se celé nastavení slotu
  včetně barevné škály,
- výběr entit Home Assistantu ve všech políčkách s ID entity; šipka v poli
  rozbalí seznam, psaní ho filtruje bez ohledu na diakritiku a ručně zadané ID
  zůstává platné. Seznam si vyžádá firmware přes `/api/template` při prvním
  otevření nabídky, tlačítko Načíst entity ho jen obnoví,
- barvu hodin, data a obou částí vteřinového efektu,
- denní/noční jas, ruční nebo automatický režim a automatické střídání hodin s radarem,
- pořadí obrazovek v záložce **Obrazovky**: přetažením nebo šipkami,
- automatické OTA aktualizace a režim webového serveru,
- volitelné heslo webového nastavení,
- export/import zálohy, restart, ovládání podsvícení a živou diagnostiku.

### Meteoradar ČHMÚ

Radar používá výhradně otevřená data radarového kompozitu MAX_Z Českého
hydrometeorologického ústavu. Nabízí pohledy 25, 50, 100 a 200 km kolem
uložené GPS polohy a přehled celé České republiky. Mapový podklad obsahuje
obrys státu a města přizpůsobená jednotlivým rozsahům.

### Zdroj srážkových dat

Radar má dva zdroje a přepínají se v nastavení webu.

Kompozice **ČHMÚ** je nad Českem ostřejší a zůstává výchozí, ale za hranicemi
nemá co ukázat. Proto je dostupná pouze pro polohy, které vyhledávání
Open-Meteo označí kódem země `CZ`. U lokality mimo Českou republiku firmware
radar ČHMÚ nespouští, nestahuje jeho data na pozadí, nereaguje na radarová
gesta a automatické střídání obrazovek vypne. Počasí Open-Meteo i Home
Assistant zůstávají bez tohoto omezení.

**RainViewer** pokrývá Evropu i svět, je zdarma a nepotřebuje klíč, jen je
hrubší. Po jeho zapnutí je radar dostupný i mimo ČR a mapový podklad se
přepne na obrysy evropských států a evropská města; česká města si přitom
ponechají zkratky, na které jsi zvyklý. RainViewer servíruje dlaždice Web
Mercatoru a jeho přiblížení jde po mocninách dvou, takže vyjde nejbližší
dostupný rozsah, a ne přesně číslo z nastavení; popisek nahoře proto ukazuje
poloměr, který opravdu vyšel. Dlaždice se necachují, takže změna rozsahu
znamená stažení animace znovu.

### Stupnice intenzity

Volitelná stupnice u levého okraje ukazuje šest odstínů s odrazivostí v dBZ a
odpovídajícími srážkami v mm/h; převod je Marshallův-Palmerův vztah
(Z = 200 R^1,6). Paleta i popisky se mění se zvoleným zdrojem, protože stejná
žlutá znamená na jedné stupnici 40 dBZ a na druhé 35; kterou právě čteš, říkají
její vlastní čísla. Zabírá kus mapy, takže ji lze vypnout; místo se pak vrátí
popiskům měst.

### Rozvržení radarové obrazovky

Obrazovka má nad mapou i pod ní pevné pásy, aby stejná informace byla vždy na
stejném řádku:

1. **ukazatel obrazovek** úplně nahoře (společný všem obrazovkám, viz níže),
2. **čas a venkovní teplota** — jde se tak na radar podívat, aniž by ses musel
   přepínat zpátky na hodiny,
3. **řada teček snímků**, jedna na snímek animace,
4. **čas snímku** — nejaktuálnější snímek se jmenuje `NYNÍ` a je v denním
   režimu jasně zelený, starší nesou svoje stáří ve tvaru `-25 min 14:10`,
5. dole **rozsah** (`50 km`, nebo `CELÁ ČR`) a pod ním **řada teček rozsahů**.
   Zdroj dat se na obrazovce nejmenuje — ani u rozsahu, ani v hlavičce
   stupnice. Vybírá se v nastavení a mění se nanejvýš jednou za život hodin,
   takže by na každém snímku jen ubíral místo; ověřit ho jde na záložce
   Meteoradar i na stránce diagnostiky.

Řádek s časem a teplotou se dá v nastavení vypnout. Teplotu bere firmware ze
zdroje, který má nastavený: u Open-Meteo z předpovědi pro uložené město, u Home
Assistantu z entity, kterou vybereš v poli **Entita venkovní teploty** na
záložce Meteoradar. Bez vyplněné entity zůstane v řádku jen čas. Teplota se
zaokrouhluje na celé stupně — desetina je u venkovní teploty šum a dva znaky
navíc rozhodují o tom, jestli se řádek do kruhu vejde.

Dokud není připravený ani jeden snímek, zůstane uprostřed prázdné obrazovky
hláška o stavu stahování a ostatní popisky se schovají.

Počet snímků lze nastavit od 1 do 15. Jeden snímek znamená statický radar;
vyšší počet vytvoří animaci od nejstaršího snímku k nejnovějšímu. Po posledním
snímku následuje nastavitelná pauza 0 až 30 sekund; výchozí hodnota je 5 sekund.
Rozsvícená tečka ukazuje, kde v animaci právě jsi, a během kompletní přípravy
prázdné cache je červená. Nová data se kontrolují v pevných pětiminutových
slotech přibližně minutu po čase publikace ČHMÚ.

### Ukazatel obrazovek

Nahoře na **každé** obrazovce je řada teček, jedna na obrazovku zapojenou do
střídání, ve stejném pořadí, jaké je nastavené v záložce Obrazovky. Plná tečka
je ta, na kterou se právě
díváš. Vypnutá obrazovka svoji tečku nemá, takže řada vždycky odpovídá tomu,
kam se dá gestem přepnout. Při jediné dostupné obrazovce se ukazatel nekreslí,
protože jedna tečka o ničem nevypovídá; v nastavení a při aktualizaci firmwaru
je také skrytý.

Při červeném nočním vzhledu se mapový podklad, města, poloha, čas i jednotlivé
stupně odrazivosti převedou do odstínů červené. Jas jednotlivých stupňů dál
vyjadřuje intenzitu srážek a nejslabší odrazy mají zachované čitelné minimum.
Čas nejaktuálnějšího snímku má stejnou červenou jako ostatní text. Převod
probíhá z připravené cache, takže přepnutí vzhledu nevyvolá nové stahování ani
přípravu animace.

Tlačítka rozsahů na webu mění právě zobrazený pohled okamžitě. Modrá označuje
aktuální rozsah na hodinách a žlutá uložený výchozí rozsah. Do trvalé
konfigurace se změna zapíše až tlačítkem **Uložit změny**. Rozsah zvolený
dotykem na displeji zůstává pouze do restartu; po něm se obnoví hodnota
naposledy uložená přes web.

Automatické střídání je ve výchozím stavu vypnuté. Po zapnutí lze nastavit
samostatnou dobu zobrazení hodin, radaru, zpráv, předpovědi i letadel; do
střídání se zapojí jen ty
obrazovky, které jsou zapnuté, a ručně otevřená obrazovka zůstane až do dalšího
gesta. Nastavený čas radaru je minimální: rozběhnutý animační cyklus se vždy
dokončí včetně závěrečné pauzy, takže přechod zpět na hodiny nepřeruší animaci
uprostřed. Po restartu
se příprava cache spustí na pozadí až po připojení Wi-Fi a synchronizaci času.
První automatický přechod na radar počká na kompletní animaci; další přechody
ji proto zobrazí okamžitě od nejstaršího snímku. Je-li automatické střídání
vypnuté, firmware radar na pozadí nestahuje a načítání začne až při ručním
otevření.

### Jmeniny

Pod datem se u českého jazyka ukazují jmeniny pro dnešní den, například
`ADAM, EVA`. Tabulka je součástí firmwaru, takže se nikam nechodí pro data:
jmeniny fungují i bez sítě a bez Home Assistanta. Kreslí se na všech třech
cifernících — digitálním, analogovém i HODNOTY.

Jména jsou psaná velkými písmeny, protože font `clock_czech` obsahuje z české
diakritiky jen velké znaky. Dny bez jmenin (1. ledna, 24. prosince a několik
dalších) zůstanou prázdné. Anglické datum jmeniny neukazuje, protože jde
o český zvyk, a se skrytým datem mizí i ony — samotné jméno bez data by na
ciferníku viselo bez souvislosti.

### Zprávy z RSS

Samostatná obrazovka umí zobrazit poslední zprávy z libovolného kanálu RSS 2.0
nebo Atom. Adresu kanálu zadáš v záložce **Zprávy** webového nastavení,
například `https://www.irozhlas.cz/rss/irozhlas`. Tlačítko **Vyzkoušet kanál**
adresu stáhne ještě před uložením a rovnou ukáže, jak budou zprávy vypadat na
displeji. Certifikát serveru se ověřuje proti kořenům Mozilly zabudovaným ve
firmwaru, takže funguje libovolná adresa `https://`.

Zobrazit lze 3 až 6 zpráv; výchozí je 5. U tří až pěti zpráv má titulek tři
řádky, což u běžné české zpravodajské věty stačí na celý titulek. Šestá zpráva
se vejde jen za cenu dvou řádků na titulek, takže se delší titulky utnou třemi
tečkami. Vlevo od titulku je čas
vydání převedený do místního času; kanál bez data se zobrazí bez času a řadí
se za zprávy s datem.

Titulky se přepisují do ASCII: z `Ř` se stane `R` a z `ř` pak `r`. Přepis
pokrývá celé bloky Latin-1 Supplement a Latin Extended-A, takže projdou i
slovenská, polská nebo německá jména, a typografické uvozovky a pomlčky se
nahradí jejich ASCII obdobou. Není to omezení písma — `ClockCzechFont*.c` mají
celou českou abecedu včetně malých písmen a jmeniny pod datem ji používají.
Kanál ale může přijít v jakémkoli jazyce, takže se sráží všechno; obrazovka
s agendou, která čte jen vlastní server, si diakritiku nechává.

Kanál se stahuje v intervalu 5 až 120 minut, výchozí je 10 minut, a to i když
je obrazovka zpráv zavřená. Otevření obrazovky gestem nebo automatickým
střídáním navíc stažení vyvolá hned, pokud jsou zprávy v mezipaměti starší než
pět minut; čerstvější se znovu nestahují, aby průlety rotace kanál nezatěžovaly.
Po neúspěchu firmware zkusí stažení znovu za dvě minuty a na displeji nechá
poslední úspěšně načtené zprávy; hláška o chybě se ukáže jen tehdy, když se
kanál nepodařilo načíst ani jednou. Vypnutá obrazovka se nestahuje vůbec a
neobjeví se ani gestem.

### Agenda z kalendáře

Samostatná obrazovka ukazuje, co je v kalendáři na nejbližší dny: řádek na
událost, nad prvním řádkem každého dne popisek `DNES`, `ZÍTRA` nebo třeba
`pá 11.9.`. Celodenní událost má místo času pomlčku a stojí na začátku svého
dne. Barva času říká, ze kterého kalendáře událost je.

**Do Googlu chodí server, ne hodiny.** Hodiny čtou hotový seznam z adresy, kterou
zadáš v záložce **Agenda** — typicky `https://tvuj-server.example.net/agenda.json`.
Server kalendáře přečte přes servisní účet, sloučí je, rozbalí opakované
události a složí i popisky dnů, takže ve firmwaru nezůstala žádná datumová
aritmetika a v hodinách žádný token. Které kalendáře se ukážou, se proto
nastavuje na serveru; návod i zdrojové soubory jsou v [infra/](infra/README.md).

Zobrazit lze 3 až 10 událostí; výchozí je 8. Každý den navíc si vezme jeden
řádek na hlavičku, takže se jich při dlouhém výhledu vejde méně. Tlačítko
**Vyzkoušet agendu** adresu stáhne ještě před uložením a ukáže, co se objeví na
displeji.

Agenda se stahuje v intervalu 5 až 120 minut, výchozí je 15 minut, a to i když
je obrazovka zavřená. Server ji stejně přepočítává po čtvrthodině, takže
častější dotaz nemá co přinést. Prázdný kalendář **není chyba**: obrazovka
řekne `Nic naplánovaného` a automatické střídání ji přeskočí, protože rotovat
na stránku, která hlásí jen prázdno, nemá cenu — podržením prstu se na ni
dostaneš pořád. Po neúspěchu firmware zkusí stažení znovu za dvě minuty a na
displeji nechá poslední úspěšně načtené události.

### Předpověď počasí

Samostatná obrazovka ukazuje předpověď z Open-Meteo pro město uložené v záložce
**Obecné**. Nezáleží na tom, odkud ciferník bere své hodnoty: souřadnice má
konfigurace i tehdy, když hodnoty čte z Home Assistantu, takže obrazovka
funguje v obou režimech. Zapíná se v záložce **Počasí**; ve výchozím stavu je
vypnutá, takže se po aktualizaci firmwaru sama neobjeví.

Nahoře je čas a venkovní teplota, stejně jako ve stavovém řádku radaru — celý
displej totiž zabírá předpověď a ciferník pod ní vidět není. Pod hlavičkou jsou
hodinové řádky (hodina, ikona, teplota, srážky, vítr) a pod dělicí čárou denní
řádky se zkratkou dne a maximem s minimem. Srážky pod desetinu milimetru se
nevypisují, aby ve sloupci nebyl les nul.

<p align="center">
  <img src="screenshots/forecast-air-quality.png" alt="Obrazovka předpovědi se sekcí kvality ovzduší" width="46%">
  <img src="screenshots/forecast-nine-hours.png" alt="Obrazovka předpovědi bez kvality ovzduší s devíti hodinami" width="46%">
</p>

Dole je volitelná sekce s evropským indexem kvality ovzduší, koncentrací PM2.5
a pylem trav; hodnoty se barví podle pásem Evropské agentury pro životní
prostředí. Pyl počítá jen evropská doména modelu CAMS, takže mimo Evropu
zůstane řádek s pomlčkou.

**Počet hodin se nenastavuje, dopočítává se.** Kruhový displej má pevný počet
řádků a každý řádek, který si vezme něco jiného, hodinám chybí. Sekce kvality
ovzduší zabírá spodní tři řádky: s ní se vejde **šest hodin**, bez ní jich je
**devět**. Stejně tak každý ubraný den (0 až 4, výchozí 3) je jedna hodina
navíc. Web u přepínače kvality ovzduší rovnou píše, kolik hodin z aktuální
kombinace vyjde a kolik by jich bylo po přepnutí; čísla počítá firmware, aby se
nemohla rozejít s tím, co obrazovka opravdu nakreslí.

Předpověď se stahuje v intervalu 10 až 180 minut, výchozí je 30 minut, a to
i když je obrazovka zavřená. Otevření obrazovky gestem nebo automatickým
střídáním navíc stažení vyvolá hned, pokud jsou data v mezipaměti starší než
čtvrt hodiny. Po neúspěchu firmware zkusí stažení znovu za dvě minuty a na
displeji nechá poslední úspěšně načtenou předpověď; hláška o chybě se ukáže jen
tehdy, když se předpověď nepodařilo načíst ani jednou. Kvalita ovzduší je
doplněk: když se nestáhne, předpověď se ukáže bez spodní sekce. Vypnutá
obrazovka se nestahuje vůbec a neobjeví se ani gestem.

### Radar letadel

Obrazovka ukazuje letadla v okolí na stejné mapě, jakou používá meteoradar.
Polohy vozí veřejné API [adsb.fi](https://opendata.adsb.fi/), trasu vybraného
letu [adsb.lol](https://api.adsb.lol/); obojí je bez klíče a bez registrace.
Poloha se bere ze stejného města jako počasí, takže se nikde nenastavuje znovu.

Barva letadla nese jeho výšku — pod 2 km červená, 2 až 6 km oranžová, 6 až
10 km žlutá a od 10 km modrá; letadlo, které výšku nehlásí, je šedé. Stupnice
s hranicemi pásem je nakreslená pod počtem letadel, takže barvu není třeba si
pamatovat. Ikona je otočená po traťovém úhlu, a když letadlo směr nevysílá,
nakreslí se místo šipky kolečko.

Dosah se přepíná přetažením prstu po displeji mezi 10, 25, 50 a 100 km, stejně
jako u meteoradaru; tečky pod popiskem ukazují, kolik kroků ještě zbývá. Dotyk
na displeji platí do restartu, po něm se obnoví hodnota uložená přes web.

V nastavení se dá zvolit **azimut, který je nahoře na displeji** — tedy směr,
kterým se z okna díváš. Otočí se celá projekce včetně mapy a měst, takže co je
na displeji nahoře, je před tebou. Neotáčí se přitom displej, jen projekce, aby
zůstalo sedět ovládání dotykem.

**Filtr výšky** nechá na mapě jen letadla v zadaném rozsahu, aby nad rušnou
oblohou bylo vidět to zajímavé. Číslo na řádku s počtem letadel říká, kolik jich
je vidět, takže se s filtrem sníží; kolik jich server poslal celkem, ukazuje
diagnostika. Hlídaného letu ani nouzových stavů se filtr netýká — ty se hledají
před ním, takže schovat nouzi kvůli nastavené výšce nejde. Letadla, která výšku
vůbec nehlásí, filtr propouští: není podle čeho je zařadit. Letadla bez volací
značky se dají skrýt zvlášť.

**Nouzový squawk** 7500 (únos), 7600 (porucha rádia) a 7700 (nouze) obtáhne
letadlo červeně a přebere řádek s počtem letadel. **Hlídaný let** zadaný volací
značkou nebo ICAO adresou dostane zelený kroužek a projde i filtrem výšky.

Klepnutím na letadlo se otevře detail s výškou, rychlostí, traťovým úhlem,
stoupáním, typem, registrací a trasou letu. Jednotky se přepínají mezi
metrickými a leteckými. Zavírá ho další klepnutí kamkoli. Výběr se drží podle
ICAO adresy letadla, ne podle pozice v seznamu: ten se staví při každém stažení
znovu a jeho pořadí není zaručené, takže by panel po chvíli ukazoval jiné
letadlo. Když letadlo z dat na chvíli zmizí, panel zůstane otevřený s
posledními známými hodnotami a přizná to poznámkou *signál ztracen*; zavře se
až po třech stahováních bez něj.

Trasa se hledá jen pro jedno vybrané letadlo, nikdy pro celý seznam, a odpověď
se drží. Spolu s volací značkou se posílá i poloha letadla a server podle ní
posoudí, jestli trasa k poloze sedí — bez toho se letadlu nad Prahou ukazovala
trasa Atény – Istanbul, protože se volací značky mezi rotacemi recyklují.
Spousta letů žádnou trasu nemá (všeobecné letectví, vojenské stroje,
vrtulníky); je to normální stav, ne chyba, a prostě se nic nezobrazí.

Dotazovat se dá jednou za 5 až 120 sekund. Větší dosahy si k nastavené hodnotě
přidají vlastní minimum — 10 sekund od 50 km a 15 sekund od 100 km — protože
vracejí víc dat a o vteřinu tam nejde; adsb.fi je API zdarma a firmware se k
němu chová jako slušný host. Stahuje se jen tehdy, když je obrazovka vidět nebo
zapojená do automatického střídání — a je-li jen ve střídání a zrovna schovaná,
ptá se nejvýš jednou za pět minut. Stačí totiž, aby měl radar snímek po ruce, až
na něj přijde řada; otevření obrazovky si stažení vynutí samo. Po neúspěchu se čeká dvojnásobek intervalu
a na displeji zůstane poslední povedený snímek: prázdná obloha po jednom
nepovedeném stažení vypadá jako pravda, ale není. Vypnutá obrazovka se
nestahuje vůbec a neobjeví se ani gestem.

### Barevné prahy měřených hodnot

Každá měřená hodnota může mít až deset dvojic **hodnota → barva**. Firmware
mezi sousedními body plynule interpoluje, takže změna barvy na displeji není
omezena jen na několik tvrdých stavů. Škály jsou nezávislé: například VOC může
používat jiné hranice než CO₂.

<p align="center">
  <img src="screenshots/web-color-scales.png" alt="Nastavení barevných prahů VOC" width="49%">
  <img src="screenshots/web-color-scales-b.png" alt="Nastavení barevných prahů CO2" width="49%">
</p>

### Jas, denní/noční režim a vteřiny

Denní i noční jas se nastavují samostatně. Automatika používá východ a západ
slunce s volitelným ranním a večerním offsetem. Volitelná entita světla může v
nočním čase dočasně aktivovat denní vzhled. Volba **Vzhled nočního režimu** je
dostupná i při vypnuté automatice, protože stejný červený vzhled lze zapnout
ručně krátkým dotykem na hodinách i meteoradaru. Samostatně lze nastavit také
barvu hodin, data, typ vteřinového efektu, velikost a jas jeho aktivní i
neaktivní části.

<p align="center">
  <img src="screenshots/web-display-settings.png" alt="Nastavení jasu, denního a nočního režimu a vteřin" width="920">
</p>

Konfigurační web je ve výchozím režimu **Vždy zapnutý**. Lze jej přepnout na
deset minut po startu nebo aktivaci z displeje, případně jej úplně vypnout.
Provozuj jej jen v důvěryhodné síti; aktivní web signalizuje ikona ozubeného
kola na dashboardu.

Webové nastavení lze chránit heslem o délce 6 až 20 znaků. Stav bez hesla je
v záložce **Systém** označený červeně, aktivní ochrana zeleně. Heslo je uložené
v zařízení jako odvozený hash, nelze je zpětně zobrazit a není součástí
exportované zálohy.

### Diagnostika

V záložce **Systém** je odkaz na samostatnou stránku `/diagnostics`, která se
otevře v novém panelu. Bez dalších měření na pozadí zobrazuje aktuální a
minimální volnou interní RAM a PSRAM, nejmenší volné místo v zásobníku úlohy
loop a datové úlohy, procesor, velikost flash, důvod restartu,
Wi-Fi, IP adresu a skutečnou frekvenci pixel clocku displeje. Dále ukazuje stav
Home Assistantu, Open-Meteo a TMEP.cz a u radaru vybrané město, GPS, rozsah, počet
připravených snímků, jejich časové rozpětí, poslední úspěšnou aktualizaci,
další plánovanou kontrolu, HTTP stav a právě zpracovávaný soubor.

### Záloha konfigurace

Exportovaná JSON záloha obsahuje vzhled a ID entit, ale neobsahuje Home
Assistant token, exportní URL TMEP.cz, heslo webu ani secret ovládacího API. Po importu proto může
být nutné citlivé hodnoty zadat znovu. Restart zařízení uložené nastavení
nemaže.

## Nastavení na displeji

Obrazovky přepíná podržení prstu na místě, zhruba půl sekundy. Záleží na tom,
kde prst leží: v levé polovině displeje se jde o obrazovku zpět, v pravé
vpřed. Výchozí pořadí je hodiny, meteoradar, zprávy, předpověď, letadla,
nastavení a zase zpět na hodiny, takže nastavení je z hodin na jedno podržení
v levé polovině. Prvních pět obrazovek si můžeš přeskládat v záložce
**Obrazovky** webového nastavení; nastavení zůstává v cyklu poslední, aby se
z něj odcházelo pokaždé stejně. Nedostupná obrazovka se přeskočí, ale své místo
si drží; nastavení vypnout nejde, aby hodiny bez radaru, zpráv i předpovědi
neztratily cestu k webové adrese.

Z nastavení se odchází stejným podržením. Neuložené změny se přitom zahodí,
uloží je jen tlačítko Uložit.

Na radaru přetažení prstu nahoru nebo doprava pohled přiblíží, dolů nebo doleva
jej oddálí. Změna provedená na displeji je dočasná a nezapisuje se do flash.

| Obrazovka a gesto | Výsledek |
| --- | --- |
| Kterákoliv: podržení prstu v levé polovině | Přepne na předchozí dostupnou obrazovku |
| Kterákoliv: podržení prstu v pravé polovině | Přepne na další dostupnou obrazovku |
| Kterákoliv: dvojklepnutí při vypnuté automatice den/noc | Přepne denní a noční režim |
| Letadla: krátký dotyk | Vybere letadlo pod prstem, nebo zavře jeho detail |
| Meteoradar: přetažení nahoru nebo doprava | Přiblíží rozsah |
| Meteoradar: přetažení dolů nebo doleva | Oddálí rozsah |

Gesta se rozpoznávají v software z hrubých souřadnic dotyku, ne z gestového
registru řadiče CST820. Vyhodnocují se až po zvednutí prstu, takže krátké tahy
po zaobleném displeji nepropadnou a jedno gesto se nezopakuje dvakrát.

Nastavení má tři stránky. Velká tlačítka se šipkami je přepínají; přetažení
prstu se uvnitř nastavení nepoužívá.

<p align="center">
  <img src="screenshots/device-settings.png" alt="První stránka nastavení denního a nočního jasu" width="31%">
  <img src="screenshots/device-settings-2.png" alt="Druhá stránka nastavení vteřin a animovaných ikon" width="31%">
  <img src="screenshots/device-settings-3.png" alt="Třetí stránka nastavení webu a OTA" width="31%">
</p>

První stránka ovládá denní a noční jas a automatický režim. Druhá přepíná
vteřiny, jejich efekt a animované ikony. Třetí řídí režim webového serveru a
ruční kontrolu OTA. IP adresa je na veřejném snímku záměrně skrytá.
Dvojklepnutí kamkoliv při vypnuté automatice přepíná denní a noční režim;
jedno klepnutí ho nepřepíná, protože se pletlo s podržením prstu.

## Animované Meteocons

Statické monochromatické ikony jsou uložené přímo ve firmware. Volitelné
animované ikony veřejného buildu se stahují z GitHub Pages a ukládají do
lokální cache. V nočním režimu se vždy použije monochromatický styl, aby ikony
respektovaly červené noční zobrazení.

V `docs/assets/weather-icons/` je pouze 45 GIFů používaných firmwarovým
allowlistem: 15 stavů pro každý ze stylů Monochrome, Flat a Line. Každý veřejný
manifest obsahuje skutečnou velikost a SHA-256 souboru; kompletní pracovní
mirror 1557 ikon v repozitáři není. Postup reprodukovatelného vytvoření je v
[`METEOCONS_ASSET_PIPELINE.md`](METEOCONS_ASSET_PIPELINE.md).

## OTA aktualizace

Release firmware používá A/B layout se dvěma stejně velkými 6MiB aplikačními
oddíly. Veřejný build čte statická metadata a OTA obraz pouze z GitHub Pages;
interní vývojový profil může dál používat Firmware Hub. Nová aplikace se
zapisuje do neaktivního slotu. Před aktivací se ověří:

- HTTPS spojení a povolený release origin,
- HTTP status a deklarovaná velikost,
- skutečný počet přijatých bajtů,
- SHA-256 obrazu,
- rodina čipu ESP32-S3,
- kapacita neaktivního aplikačního oddílu.

Při chybě zůstane aktivní stávající firmware. Wi-Fi a konfigurace v NVS a
`clockcfg` se při běžné OTA aktualizaci zachovají. Factory instalace nebo
vymazání celé flash je jiná operace a může uživatelská data odstranit.

Verze 1.6.0 podporuje jedinou historickou migraci konfigurace z veřejné verze
1.5.5. Zachová dosavadní zdroj dat, Home Assistant, entity, vzhled a další
uložené hodnoty a doplní nové radarové volby. U migrovaného zařízení se radar
nastaví na celou ČR, 6 snímků a automatické střídání zůstane vypnuté. Starší
vývojové meziverze nejsou samostatně podporované migračními kroky. Přechod z
1.5.5 na 1.6.0 byl ověřen skutečnou A/B OTA aktualizací včetně zachování
uložené konfigurace.

Automatické OTA aktualizace jsou po čisté instalaci vypnuté. Po zapnutí ve
webu firmware nejvýše jednou denně po 4:10 lokálního času zkontroluje novou
SemVer a případně ji nainstaluje. Stejnou cestu používá ruční aktualizace.

## Ovládací API pro Home Assistant

Web zobrazuje URL ovládacího endpointu obsahující náhodný 128bitový secret.
Pomocí REST příkazů lze aktualizovat data, zapnout či vypnout podsvícení nebo
vyvolat další podporované akce. URL považuj za přihlašovací údaj: nevkládej ji
do screenshotů, veřejných logů ani Git repozitáře.

Secret je uložený v zařízení, ověřuje se konstantním časem a není součástí
exportované zálohy. Přesný tvar endpointů a příklady požadavků jsou zobrazené
přímo v aktuálním webovém rozhraní firmware.

## Sestavení ze zdrojů

### Závislosti

Ověřený toolchain používá:

- Arduino CLI,
- Arduino ESP32 core `3.0.7`,
- LVGL `8.3.10`,
- PNGdec `1.0.1`,
- Python 3 pro generátory a release balíček.

Na macOS lze závislosti nainstalovat například takto:

```sh
arduino-cli core install esp32:esp32@3.0.7 --config-file arduino-cli.yaml
arduino-cli lib install lvgl@8.3.10 --config-file arduino-cli.yaml
arduino-cli lib install PNGdec@1.0.1 --config-file arduino-cli.yaml
```

Přenositelná konfigurace Arduino CLI je v `arduino-cli.yaml`. Lokální
ignorovaný soubor `WaveshareHodiny/local/arduino-cli.yaml` ji může přepsat.

### Vývojový build

```sh
./build.sh
./upload.sh
```

`./build.sh` používá výchozí domácí údaje `WIFI_SSID` a `WIFI_PASSWORD`.
Pracovní profil sestavíš pomocí `./build.sh work`; ten použije samostatné
hodnoty `WIFI_WORK_SSID` a `WIFI_WORK_PASSWORD`.

Volitelný port lze předat explicitně:

```sh
./upload.sh /dev/cu.usbmodemXXXXXXXX
```

Vývojový build se ukládá do `build/waveshare-hodiny-develop/`, podporuje USB
diagnostiku a screenshoty a úmyslně neinstaluje OTA release. Bez `.env` se
stále sestaví, pouze nemá vývojové výchozí Wi-Fi a HA hodnoty.

### Volitelná lokální `.env`

`.env` je celý ignorovaný Gitem a není pro sestavení povinný. Generátor
podporuje tyto lokální proměnné:

```dotenv
WIFI_SSID=
WIFI_PASSWORD=
WIFI_WORK_SSID=
WIFI_WORK_PASSWORD=
HOME_ASSISTANT_URL=
HOME_ASSISTANT_TOKEN=
HA_ENTITY_WEATHER_CODE=
HA_ENTITY_OUTSIDE_TEMPERATURE=
HA_ENTITY_ROOM_TEMPERATURE=
HA_ENTITY_ROOM_CO2=
HA_ENTITY_ROOM_HUMIDITY=
HA_ENTITY_SUN=
FIRMWARE_SERVER_URL=
FIRMWARE_PROJECT_SLUG=
```

Skutečné hodnoty nikdy necommituj. Generované headery se ukládají pouze do
ignorovaného adresáře `WaveshareHodiny/local/`.

### Release build

Verzi zvol jako platný SemVer 2.0.0:

```sh
./build-release.sh 1.0.0
```

Výsledek je v `build/waveshare-hodiny-release/1.0.0/`. Adresář `package/`
obsahuje instalační části pro ESP Web Tools a právě jeden samostatný
`.ota.bin`. Release build neobsahuje lokální Wi-Fi ani Home Assistant údaje.

Tento výchozí příkaz zachovává interní profil z lokální `.env`. Veřejný profil
pro GitHub Pages lze lokálně pouze sestavit takto:

```sh
RELEASE_CHANNEL=public ./build-release.sh 1.0.0
```

Jeho výsledek je v `build/waveshare-hodiny-release/1.0.0-public/` a kromě
factory částí obsahuje také statická `ota.json` metadata. Nepoužívá `.env`,
lokální Wi-Fi, Home Assistant údaje ani klíč Firmware Hubu.

Žádný lokální build nic nepublikuje. Ruční GitHub Actions workflow **Public
firmware release** vyžaduje konkrétní stabilní SemVer a má samostatný přepínač
pro vytvoření neměnného GitHub Release. Bez něj pouze sestaví a zkontroluje
dočasný artifact. Pages z nejnovějšího stabilního GitHub Release přebírá čtyři
factory části, instalační manifest, samostatný OTA obraz a jeho metadata.

## Screenshot displeje přes USB

Vývojový firmware umí odeslat RGB565 framebuffer příkazem `SCREENSHOT`.
Pomocný nástroj jej převede na transparentní kruhové PNG 480 × 480 px:

```sh
./capture-screenshot.sh --output screenshots/latest.png
./capture-screenshot.sh --settings --output screenshots/settings.png
./capture-screenshot.sh --settings-page 2 --output screenshots/settings-2.png
./capture-screenshot.sh --night --output screenshots/night.png
```

Pokud je připojeno více zařízení, předej `--port`. Nástroj používá pyserial
3.5 z lokálního ignorovaného adresáře `.arduino/python`.

## Struktura repozitáře

```text
WaveshareHodiny/        Arduino sketch a firmware
assets/                 Zdrojové assety použité generátory
docs/assets/            Jen veřejně používané animované GIFy a manifesty
infra/                  Kopie serverové části (proxy a generátor zpráv)
screenshots/            Veřejné obrázky dokumentace
tools/                  Build, test a asset utility
WaveshareHodiny/partitions.csv
                        Vlastní 16MiB A/B partition table
build.sh                Vývojový build
build-release.sh        Oddělený release build
upload.sh               USB upload vývojového buildu
tools/check-stack.sh    Kontrola serverové části (kanál, HA, certifikát)
```

Obrazovka se zprávami a hodnoty z Home Assistanta chodí přes vlastní server.
Co na něm běží, jak se obnovuje a na co si dát pozor popisuje
[infra/README.md](infra/README.md); stav se ověří příkazem
`tools/check-stack.sh`.

## Řešení problémů

### `waveshare-hodiny.local` se neotevře

- ověř ikonu Wi-Fi na displeji,
- použij IP adresu z nastavení zařízení,
- pokud je zvolený časově omezený nebo vypnutý režim webu, otevři nastavení
  dlouhým stiskem kdekoliv na hodinách nebo meteoradaru,
- zkontroluj, že klient i zařízení jsou ve stejné dosažitelné síti.

Samostatná stránka `http://<IP-adresa>/diagnostics` zůstává dostupná i při
zamčeném konfiguračním webu.

### Home Assistant test selže

- URL musí obsahovat `http://` nebo `https://`,
- ověř token a přesná ID entit,
- při změně URL zadej také nový token,
- zkontroluj firewall mezi IoT sítí a Home Assistantem.

### Hodnota zůstává `--`

Otevři v Home Assistantu **Vývojářské nástroje → Stavy** a ověř, že entita
existuje a její stav je číselný nebo podporovaný stav počasí.

### OTA aktualizace není dostupná

Vývojový build OTA neinstaluje. U release buildu ověř připojení k internetu,
synchronizovaný čas a dostupnost nakonfigurovaného HTTPS release serveru.
Veřejný build používá `https://teffteff.github.io/waveshare-hodiny/firmware/`;
interní profil může používat jiný server z lokální `.env`.

### Zařízení se neobjeví na USB

Vyzkoušej oba USB-C konektory a datový kabel. Pro první factory instalaci může
být nutné uvést ESP32-S3 do bootloaderu podle dokumentace Waveshare.

## Bezpečnost a soukromí

- žádné Wi-Fi heslo ani HA token není součástí veřejného release,
- secrets, lokální buildy a generované headery jsou ignorované Gitem,
- HA token se po uložení neposílá zpět do prohlížeče,
- konfigurační web lze chránit heslem; bez nastaveného hesla patří pouze do
  důvěryhodné LAN,
- veřejná diagnostika nezobrazuje hesla, tokeny ani secret ovládacího API,
- HA HTTPS aktuálně toleruje neověřený/self-signed certifikát,
- OTA používá samostatná přísnější ověření TLS, originu, velikosti a SHA-256,
- ovládací API URL obsahuje secret a nesmí se zveřejňovat.

Před nahlášením bezpečnostního problému nezveřejňuj funkční token, Wi-Fi heslo
ani ovládací URL v issue.

## Poděkování

Při implementaci meteoradaru jsem využil a pro potřeby tohoto firmware
přizpůsobil část kódu z open-source projektu
[MeteoPlaneRadar](https://github.com/petus/MeteoPlaneRadar), který vyvíjí
Petr z [Chiptron.cz](https://chiptron.cz/). Děkuji za zveřejnění projektu,
praktickou ukázku práce s radarovými daty ČHMÚ a mapové podklady, na kterých
jsem mohl tuto integraci postavit.

## Licence

Původní kód projektu je dostupný pod [MIT licencí](LICENSE). Firmware používá
knihovny, fonty a grafické assety s vlastními licencemi; jejich autoři,
licence a zdrojové odkazy jsou uvedené v
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). MIT licence projektu jejich
původní licenční podmínky nenahrazuje.
