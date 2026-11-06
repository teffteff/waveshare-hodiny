#pragma once

#include <stddef.h>
#include <stdint.h>

// Notifikace na displeji z ovládacího API:
//   POST /api/control/<secret>/notification
// s JSONem (application/json) nebo formulářem se stejnými jmény polí.
// Převzato z upstreamu (CooLajz, cca8a78): nadpis, zpráva, barvy, doba
// zobrazení a délka pípnutí. Notifikace leží nad vším, klepnutí ji zavře a nová
// nahradí předchozí; fronta ani uložení přes restart nejsou.
//
// Tady je jen čtení a kontrola požadavku, bez Arduina, aby šla testovat na
// počítači (tools/test_display_notification.cpp).

constexpr size_t NOTIFICATION_TITLE_MAX_BYTES = 96;
constexpr size_t NOTIFICATION_MESSAGE_MAX_BYTES = 768;
constexpr uint32_t NOTIFICATION_MAX_DURATION_SECONDS = 86400;
constexpr uint16_t NOTIFICATION_MAX_BEEP_MS = 5000;

struct DisplayNotification {
  // UTF-8. Nadpis je jednořádkový (konce řádků se změní na mezery), zpráva
  // smí obsahovat \n.
  char title[NOTIFICATION_TITLE_MAX_BYTES + 1] = "";
  char message[NOTIFICATION_MESSAGE_MAX_BYTES + 1] = "";
  uint32_t textColor = 0xFFFFFF;
  uint32_t backgroundColor = 0x000000;
  // 0 = pevná notifikace, zavře ji jen klepnutí nebo další požadavek.
  uint32_t durationSeconds = 0;
  // Délka jednoho pípnutí; 0 = bez zvuku. Pípá jen kulatý 2,1" (bzučák na
  // EXIO8), 7" bzučák nemá.
  uint16_t beepMs = 0;
};

// Pole požadavku už rozkódovaná do textu; nullptr = pole chybí. Délku textu
// kontroluje parser, proto mají buffery volajícího rezervu nad limity.
struct NotificationFields {
  const char *title = nullptr;
  const char *message = nullptr;
  const char *durationSeconds = nullptr;
  const char *beep = nullptr;
  const char *textColor = nullptr;
  const char *backgroundColor = nullptr;
};

// Vrací nullptr, když je požadavek v pořádku, jinak českou chybovou hlášku
// pro odpověď 400.
const char *notificationFromFields(const NotificationFields &fields,
                                   DisplayNotification &out);
// Pracovní buffery pro text z JSONu, s rezervou nad limity (příliš dlouhý
// text musí parser poznat, ne dostat oříznutý). Volající je drží mimo interní
// RAM - firmware v PSRAM, viz handleNotificationRequest.
struct NotificationScratch {
  char title[NOTIFICATION_TITLE_MAX_BYTES * 2 + 8];
  char message[NOTIFICATION_MESSAGE_MAX_BYTES * 2 + 8];
};
const char *notificationFromJson(const char *body, size_t length,
                                 DisplayNotification &out,
                                 NotificationScratch &scratch);

// Barva ve tvaru #RRGGBB (i bez mřížky).
bool notificationParseColor(const char *text, uint32_t &out);
