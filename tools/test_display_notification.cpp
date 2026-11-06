// Čtení notifikace z ovládacího API: JSON i formulář, limity, barvy, UTF-8.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "DisplayNotification.h"

namespace {

NotificationScratch scratch;

const char *parseJson(const std::string &body, DisplayNotification &out) {
  return notificationFromJson(body.data(), body.size(), out, scratch);
}

void testFullJson() {
  DisplayNotification n;
  const char *error = parseJson(
      R"({"title":"Pračka doprala","message":"Prádlo už můžeš\npověsit.",)"
      R"("durationSeconds":15,"beep":150,"textColor":"#FFFFFF",)"
      R"("backgroundColor":"#124734"})",
      n);
  assert(error == nullptr);
  assert(strcmp(n.title, "Pračka doprala") == 0);
  assert(strcmp(n.message, "Prádlo už můžeš\npověsit.") == 0);
  assert(n.durationSeconds == 15);
  assert(n.beepMs == 150);
  assert(n.textColor == 0xFFFFFF);
  assert(n.backgroundColor == 0x124734);
}

void testDefaultsAndEscapes() {
  DisplayNotification n;
  // \u escapy, čísla jako řetězce, null jako chybějící pole.
  assert(parseJson(R"({"title":" Zvonek\n ","message":"Někdo je u dveří",)"
                   R"("durationSeconds":"0","beep":null})",
                   n) == nullptr);
  assert(strcmp(n.title, "Zvonek") == 0);
  assert(strcmp(n.message, "Někdo je u dveří") == 0);
  assert(n.durationSeconds == 0);
  assert(n.beepMs == 0);
  assert(n.textColor == 0xFFFFFF);
  assert(n.backgroundColor == 0x000000);
  // V nadpisu se konec řádku změní na mezeru, \r zmizí všude.
  assert(parseJson(R"({"title":"A\nB","message":"x\r\ny"})", n) == nullptr);
  assert(strcmp(n.title, "A B") == 0);
  assert(strcmp(n.message, "x\ny") == 0);
}

void testRejections() {
  DisplayNotification n;
  assert(parseJson("", n) != nullptr);
  assert(parseJson("[1]", n) != nullptr);
  assert(parseJson(R"({"title":"A")", n) != nullptr);
  assert(parseJson(R"({"message":"x"})", n) != nullptr);
  assert(parseJson(R"({"title":"x"})", n) != nullptr);
  assert(parseJson(R"({"title":"   ","message":"x"})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","durationSeconds":86401})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","durationSeconds":-1})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","durationSeconds":1.5})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","durationSeconds":86400})", n) == nullptr);
  assert(parseJson(R"({"title":"x","message":"y","beep":5001})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","beep":true})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","textColor":"red"})", n) != nullptr);
  assert(parseJson(R"({"title":"x","message":"y","backgroundColor":"#12345"})", n) != nullptr);
  // Neúspěch nesmí přepsat předchozí výsledek.
  assert(parseJson(R"({"title":"stará","message":"zpráva"})", n) == nullptr);
  assert(parseJson(R"({"title":"nová","message":"y","beep":-5})", n) != nullptr);
  assert(strcmp(n.title, "stará") == 0);
}

void testLengthLimits() {
  DisplayNotification n;
  const std::string title96(96, 'a');
  const std::string message768(768, 'b');
  assert(parseJson("{\"title\":\"" + title96 + "\",\"message\":\"" + message768 + "\"}", n) ==
         nullptr);
  assert(strlen(n.title) == 96 && strlen(n.message) == 768);
  assert(parseJson("{\"title\":\"" + title96 + "a\",\"message\":\"x\"}", n) != nullptr);
  assert(parseJson("{\"title\":\"x\",\"message\":\"" + message768 + "b\"}", n) != nullptr);
  // Limit je v bajtech UTF-8: 48 x "č" = 96 bajtů projde, 49 ne. Hláška
  // u příliš dlouhého textu mluví o délce, ne o rozbitém UTF-8.
  std::string czech;
  for (int i = 0; i < 48; ++i) czech += "č";
  assert(parseJson("{\"title\":\"" + czech + "\",\"message\":\"x\"}", n) == nullptr);
  const char *error = parseJson("{\"title\":\"" + czech + "č\",\"message\":\"x\"}", n);
  assert(error != nullptr && strstr(error, "96") != nullptr);
  // Velmi dlouhý text, který JSON kopie usekne, taky jen "příliš dlouhý".
  error = parseJson("{\"title\":\"x\",\"message\":\"" + std::string(5000, 'z') + "\"}", n);
  assert(error != nullptr && strstr(error, "768") != nullptr);
}

void testUtf8() {
  DisplayNotification n;
  NotificationFields fields;
  fields.title = "ok";
  fields.message = "\xC3\x28";  // useknutá dvoubajtová sekvence
  assert(notificationFromFields(fields, n) != nullptr);
  fields.message = "\xC0\xAF";  // přetažený zápis '/'
  assert(notificationFromFields(fields, n) != nullptr);
  fields.message = "\xED\xA0\x80";  // náhradní pár
  assert(notificationFromFields(fields, n) != nullptr);
  fields.message = "Žluťoučký kůň \xE2\x80\x93 \xF0\x9F\x94\x94";
  assert(notificationFromFields(fields, n) == nullptr);
}

void testFormFields() {
  DisplayNotification n;
  NotificationFields fields;
  fields.title = "Pošta";
  fields.message = "Přišel balík";
  fields.durationSeconds = "30";
  fields.beep = "0";
  fields.textColor = "ffcc00";
  fields.backgroundColor = "#000000";
  assert(notificationFromFields(fields, n) == nullptr);
  assert(n.durationSeconds == 30 && n.textColor == 0xFFCC00);
  fields.durationSeconds = "";
  assert(notificationFromFields(fields, n) != nullptr);
}

void testColor() {
  uint32_t color = 0;
  assert(notificationParseColor("#aBcDeF", color) && color == 0xABCDEF);
  assert(notificationParseColor("000001", color) && color == 1);
  assert(!notificationParseColor("#GGGGGG", color));
  assert(!notificationParseColor("#1234567", color));
  assert(!notificationParseColor(nullptr, color));
}

}  // namespace

int main() {
  testFullJson();
  testDefaultsAndEscapes();
  testRejections();
  testLengthLimits();
  testUtf8();
  testFormFields();
  testColor();
  puts("display notification OK");
  return 0;
}
