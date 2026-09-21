#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../WaveshareHodiny/SkyFeed.h"

namespace {

bool parse(const char *text, SkyFeed &feed) {
  return skyFeedParse(text, text + strlen(text), feed);
}

// Zkrácená odpověď serveru pro Ondřejov 21. 9. 2026 22:00 SELČ.
const char RESPONSE[] =
    "{\"v\":1,\"time\":1790020800,"
    "\"bodies\":["
    "{\"id\":\"sun\",\"n\":\"Slunce\",\"k\":0,\"ra\":11.9299,\"dec\":0.453,"
    "\"au\":1.004,\"con\":\"Panna\",\"rise\":1790052420,\"set\":1790096460},"
    "{\"id\":\"moon\",\"n\":\"M\\u011bs\\u00edc\",\"k\":1,\"ra\":20.2727,"
    "\"dec\":-23.127,\"dist\":389012,\"ill\":0.763,\"con\":\"Kozoroh\","
    "\"set\":1790033220},"
    "{\"id\":\"saturn\",\"n\":\"Saturn\",\"k\":2,\"ra\":0.8235,\"dec\":2.366,"
    "\"au\":8.451,\"mag\":0.4,\"con\":\"Velryba\",\"rise\":1790099580},"
    "{\"id\":\"broken\",\"n\":\"Nic\",\"k\":2,\"dec\":2.0},"
    "{\"id\":\"jupiter\",\"n\":\"Jupiter\",\"k\":2,\"ra\":9.3679,\"dec\":16.014,"
    "\"au\":5.8,\"mag\":-1.8,\"con\":\"Rak\"}],"
    "\"stars\":[6752,-1672,-15,18616,3878,0,7577,3189,11],"
    "\"lines\":[0,1,1,2,2,9],"
    "\"events\":[{\"t\":1790789608,\"tm\":1,\"k\":\"conj\","
    "\"x\":\"M\\u011bs\\u00edc 0,2\\u00b0 od Plej\\u00e1d\"},"
    "{\"t\":1791107908,\"tm\":0,\"k\":\"opposition\",\"x\":\"Saturn v opozici\"},"
    "{\"t\":1817198114,\"tm\":1,\"k\":\"eclipse\","
    "\"x\":\"\\u010c\\u00e1ste\\u010dn\\u00e9 zatm\\u011bn\\u00ed Slunce 42 %\"}],"
    "\"kp\":2.33,\"kpMax\":6.33,\"kpMaxAt\":1790046000}";

constexpr double LATITUDE = 49.90461;
constexpr double LONGITUDE = 14.7842;
constexpr double EPOCH = 1790020800.0;

void expectHorizontal(double ra, double dec, float azimuth, float altitude,
                      float tolerance) {
  float az = 0.0f;
  float alt = 0.0f;
  skyHorizontal(ra, dec, LATITUDE, LONGITUDE, EPOCH, az, alt);
  float azimuthError = std::fabs(az - azimuth);
  if (azimuthError > 180.0f) azimuthError = 360.0f - azimuthError;
  // Azimut se u tělesa vysoko nad obzorem mění rychle; chyba se počítá na
  // obloze, ne ve stupních azimutu.
  azimuthError *= std::cos(altitude * 0.0174533f);
  if (azimuthError > tolerance || std::fabs(alt - altitude) > tolerance) {
    printf("ra %.4f dec %.3f: az %.2f alt %.2f, čekáno %.2f %.2f\n", ra, dec,
           az, alt, azimuth, altitude);
    assert(false);
  }
}

}  // namespace

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  SkyFeed feed;
  assert(parse(RESPONSE, feed));
  assert(feed.serverTime == 1790020800U);
  // Těleso bez rektascenze se přeskočí.
  assert(feed.bodyCount == 4);
  assert(strcmp(feed.bodies[1].name, "Měsíc") == 0);
  assert(feed.bodies[1].kind == SKY_BODY_MOON);
  assert(feed.bodies[1].distanceKm == 389012U);
  assert(std::fabs(feed.bodies[1].illumination - 0.763f) < 1e-4f);
  assert(feed.bodies[1].rise == 0 && feed.bodies[1].set == 1790033220);
  assert(feed.bodies[2].hasMagnitude && std::fabs(feed.bodies[2].magnitude - 0.4f) < 1e-4f);
  assert(!feed.bodies[0].hasMagnitude);
  assert(strcmp(feed.bodies[2].constellation, "Velryba") == 0);
  assert(strcmp(feed.bodies[3].id, "jupiter") == 0);
  assert(feed.starCount == 3);
  assert(feed.stars[0].raMilliHours == 6752 && feed.stars[0].decCentiDeg == -1672 &&
         feed.stars[0].magnitudeTenths == -15);
  // Čára na neexistující hvězdu se zahodí.
  assert(feed.lineCount == 2);
  assert(feed.lines[1].first == 1 && feed.lines[1].second == 2);
  assert(feed.eventCount == 3);
  assert(feed.events[0].hasTime && !feed.events[1].hasTime);
  assert(strcmp(feed.events[0].text, "Měsíc 0,2° od Plejád") == 0);
  assert(feed.hasKp && std::fabs(feed.kp - 2.33f) < 1e-4f);
  assert(feed.hasKpMax && std::fabs(feed.kpMax - 6.33f) < 1e-4f);
  assert(feed.kpMaxAt == 1790046000);

  // Bez Kp (NOAA neodpovědělo) se prostě nic neukáže.
  assert(parse("{\"v\":1,\"bodies\":[]}", feed));
  assert(!feed.hasKp && !feed.hasKpMax && feed.bodyCount == 0);
  // Jiná verze nebo chybějící tělesa: formát, kterému hodiny nerozumí.
  assert(!parse("{\"v\":2,\"bodies\":[]}", feed));
  assert(!parse("{\"v\":1}", feed));
  assert(!parse("<html>", feed));
  // Useknutá trojice hvězd ukončí seznam, nerozhodí ho.
  assert(parse("{\"v\":1,\"bodies\":[],\"stars\":[1,2,3,4,99999,5]}", feed));
  assert(feed.starCount == 1);

  // --- adresa ---
  char url[200];
  assert(skyFeedBuildUrl("https://hodiny:heslo@server/satellites.json", 49.90461f,
                         14.7842f, false, url, sizeof(url)));
  assert(strcmp(url, "https://hodiny:heslo@server/satellites.json?view=sky&"
                     "lat=49.9046&lon=14.7842&lang=cs") == 0);
  assert(skyFeedBuildUrl("https://server/s?a=1", 1.0f, 2.0f, true, url, sizeof(url)));
  assert(strstr(url, "?a=1&view=sky") != nullptr && strstr(url, "lang=en") != nullptr);
  assert(!skyFeedBuildUrl("https://server/satellites.json", 1.0f, 2.0f, false, url, 30));

  // --- poloha na obloze proti skyfieldu (s refrakcí) ---
  expectHorizontal(20.2727, -23.127, 190.92f, 16.32f, 0.3f);  // Měsíc
  expectHorizontal(0.8235, 2.366, 115.06f, 22.58f, 0.3f);     // Saturn
  expectHorizontal(4.2512, 21.094, 62.79f, 5.15f, 0.3f);      // Uran nízko
  expectHorizontal(11.9299, 0.453, 309.17f, -27.48f, 0.3f);   // Slunce pod obzorem
  expectHorizontal(9.3679, 16.014, 354.72f, -23.93f, 0.3f);   // Jupiter pod severem
  // Hvězdy chodí v J2000 bez precese; ta dělá do půl stupně.
  expectHorizontal(18.616, 38.78, 260.3f, 62.3f, 0.5f);       // Vega

  // --- datum úkazu ---
  char when[32];
  const int64_t now = 1790020800;  // 21. 9. 2026 22:00 SELČ
  SkyEvent event;
  event.hasTime = true;
  event.time = now + 3600;  // 23:00 téhož dne
  skyEventWhen(event, now, false, when, sizeof(when));
  assert(strcmp(when, "DNES 23:00") == 0);
  event.time = now + 8 * 3600;  // 6:00 zítra
  skyEventWhen(event, now, false, when, sizeof(when));
  assert(strcmp(when, "ZÍTRA 06:00") == 0);
  skyEventWhen(event, now, true, when, sizeof(when));
  assert(strcmp(when, "TMRW 06:00") == 0);
  event.time = 1790789608;  // 30. 9.
  skyEventWhen(event, now, false, when, sizeof(when));
  assert(strcmp(when, "30. 9.") == 0);
  skyEventWhen(event, now, true, when, sizeof(when));
  assert(strcmp(when, "SEP 30") == 0);
  event.time = 1817198114;  // 2. 8. 2027: jiný rok, i s rokem
  skyEventWhen(event, now, false, when, sizeof(when));
  assert(strcmp(when, "2. 8. 2027") == 0);
  event.hasTime = false;
  event.time = now + 3600;
  skyEventWhen(event, now, false, when, sizeof(when));
  assert(strcmp(when, "DNES") == 0);

  puts("sky feed OK");
  return 0;
}
