#pragma once

#include <stddef.h>
#include <stdint.h>

// Strop pro jeden rozebraný kanál. Volající si vyžádá jen tolik zpráv, kolik
// jich obrazovka kreslí (3-6); rezerva navíc drží parser nezávislý na
// ClockConfig, aby šel testovat na počítači.
constexpr size_t RSS_MAX_ITEMS = 8;
// Titulek po přepisu do ASCII. Naměřený strop iROZHLAS.cz je 106 znaků; do
// dvou řádků se jich vejde kolem sta, zbytek utne LVGL třemi tečkami.
constexpr size_t RSS_TITLE_LENGTH = 160;
constexpr size_t RSS_CHANNEL_TITLE_LENGTH = 48;

struct RssItem {
  char title[RSS_TITLE_LENGTH] = "";
  // Čas vydání v sekundách od epochy UTC. Na místní čas ho převádí až
  // RssService, protože jen zařízení zná nastavenou časovou zónu.
  bool timeAvailable = false;
  int64_t publishedAt = 0;
};

struct RssFeed {
  char channelTitle[RSS_CHANNEL_TITLE_LENGTH] = "";
  size_t count = 0;
  RssItem items[RSS_MAX_ITEMS];
};

// Přepis textu kanálu do znaků, které projektová písma opravdu obsahují.
// Řeší tři věci najednou, aby se text nemusel kopírovat přes mezivýsledky:
// dekóduje XML entity (&amp;, &#345;), dekóduje UTF-8 a výsledný kódový bod
// přeloží na ASCII (Ř na R, ř na r). Souvislé bílé znaky se sloučí do jedné
// mezery a výsledek se ořízne, protože titulky v XML bývají odsazené.
//
// Vrací délku zapsaného řetězce bez koncové nuly. Do destination se vždy
// zapíše platný řetězec, i když se text nevejde celý.
size_t rssTransliterate(const char *source, size_t sourceLength,
                        char *destination, size_t destinationSize);

// Datum z RSS (RFC 822: "Wed, 02 Sep 2026 16:42:00 +0200") nebo z Atomu
// (ISO 8601: "2026-09-02T16:42:00+02:00"). Výsledek je vždy v UTC.
bool rssParseDate(const char *text, size_t length, int64_t &unixSeconds);

// Načte RSS 2.0 i Atom. maximumItems omezuje, kolik zpráv si feed nechá;
// hodnota nad RSS_MAX_ITEMS se ořízne na strop.
bool rssParseFeed(const char *payload, size_t length, size_t maximumItems,
                  RssFeed &feed, char *error, size_t errorSize);
