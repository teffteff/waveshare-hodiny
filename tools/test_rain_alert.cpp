#include <cassert>
#include <cstdio>
#include <cstring>

#include "../WaveshareHodiny/RainAlert.h"

namespace {

bool parse(const char *text, RainForecast &forecast) {
  return rainForecastParse(text, text + strlen(text), forecast);
}

// Skutečná odpověď serveru, zachycená 20. 9. 2026.
const char REAL_RESPONSE[] =
    "{\"time\":1789916270,\"slot\":1789916100,\"covered\":true,\"step\":10,"
    "\"now\":16,\"steps\":[24,16,12,20,28,28]}";

}  // namespace

int main() {
  RainForecast forecast;

  // --- rozbor odpovědi ---
  assert(parse(REAL_RESPONSE, forecast));
  assert(forecast.now == 16);
  assert(forecast.stepCount == 6);
  assert(forecast.stepMinutes == 10);
  assert(forecast.covered);
  // Unixový čas se nesmí zaokrouhlit přes float.
  assert(forecast.slot == 1789916100U);
  assert(forecast.serverTime == 1789916270U);
  assert(forecast.steps[0] == 24 && forecast.steps[5] == 28);

  // Chybějící snímek je -1 a nesmí se splést s nulou.
  assert(parse("{\"covered\":true,\"step\":10,\"now\":0,\"steps\":[-1,0,-1]}",
               forecast));
  assert(forecast.now == 0);
  assert(forecast.steps[0] == -1 && forecast.steps[1] == 0);

  // Mimo dosah radaru server vrací samé -1 a covered:false.
  assert(parse("{\"covered\":false,\"step\":10,\"now\":-1,"
               "\"steps\":[-1,-1,-1,-1,-1,-1]}",
               forecast));
  assert(!forecast.covered && forecast.now == -1);

  // Odpověď bez "now" je rozbitá; chybějící "covered" znamená false.
  assert(!parse("{\"covered\":true,\"steps\":[10]}", forecast));
  assert(parse("{\"now\":5,\"steps\":[10]}", forecast));
  assert(!forecast.covered);

  // Useknutá odpověď nesmí číst za koncem ani projít.
  assert(!parse("{\"now\":5,\"steps\":[10", forecast));
  assert(!parse("", forecast));
  assert(!parse("neco jineho", forecast));

  // Víc kroků, než se vejde, se zahodí bez přetečení.
  assert(parse("{\"covered\":true,\"now\":0,\"steps\":"
               "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}",
               forecast));
  assert(forecast.stepCount == RAIN_FORECAST_MAX_STEPS);

  // --- rozhodnutí ---
  RainAlertDecision decision;

  // Skutečná odpověď: nad hodinami 16 dBZ, kroky 24, 16, 12, 20, 28, 28.
  // S prahem 28 prvních pět kroků neprojde a rozhodne až +50 minut.
  assert(parse(REAL_RESPONSE, forecast));
  assert(rainAlertEvaluate(forecast, 28, 60, decision));
  assert(decision.raise && decision.minutesAway == 50 && decision.dbz == 28);
  assert(!decision.rainingNow);

  // Kratší horizont tentýž déšť neuvidí.
  assert(!rainAlertEvaluate(forecast, 28, 30, decision));
  assert(!decision.raise);

  // Nižší práh ho najde dřív.
  assert(rainAlertEvaluate(forecast, 20, 60, decision));
  assert(decision.minutesAway == 10 && decision.dbz == 24);

  // Když prší už teď, nehlásí se nic: upozornění je na příchozí déšť.
  assert(parse("{\"covered\":true,\"step\":10,\"now\":40,"
               "\"steps\":[40,40,40,40,40,40]}",
               forecast));
  assert(!rainAlertEvaluate(forecast, 28, 60, decision));
  assert(decision.rainingNow && !decision.raise);

  // Slabý déšť teď a silný za chvíli se ohlásit má.
  assert(parse("{\"covered\":true,\"step\":10,\"now\":20,"
               "\"steps\":[20,44,44]}",
               forecast));
  assert(rainAlertEvaluate(forecast, 28, 60, decision));
  assert(!decision.rainingNow && decision.minutesAway == 20);

  // Mimo dosah radaru se nehlásí nic, i kdyby čísla vypadala jakkoli.
  assert(parse("{\"covered\":false,\"step\":10,\"now\":0,\"steps\":[50,50]}",
               forecast));
  assert(!rainAlertEvaluate(forecast, 28, 60, decision));

  // Chybějící snímek se přeskočí, ne že by platil za sucho.
  assert(parse("{\"covered\":true,\"step\":10,\"now\":0,\"steps\":[-1,40]}",
               forecast));
  assert(rainAlertEvaluate(forecast, 28, 60, decision));
  assert(decision.minutesAway == 20);

  // Horizont přesně na hranici kroku ten krok ještě bere.
  assert(parse("{\"covered\":true,\"step\":10,\"now\":0,\"steps\":[0,0,40]}",
               forecast));
  assert(rainAlertEvaluate(forecast, 28, 30, decision));
  assert(decision.minutesAway == 30);
  assert(!rainAlertEvaluate(forecast, 28, 29, decision));

  // --- adresa dotazu ---
  char url[128] = "";
  assert(rainFeedBuildUrl("https://hodiny:heslo@example.net/rain.json",
                          49.90461f, 14.7842f, 5, url, sizeof(url)));
  assert(strcmp(url, "https://hodiny:heslo@example.net/rain.json"
                     "?lat=49.90461&lon=14.78420&r=5") == 0);

  // Adresa, která už dotaz má, dostane &.
  assert(rainFeedBuildUrl("https://example.net/rain.json?x=1", 1.0f, 2.0f, 30,
                          url, sizeof(url)));
  assert(strstr(url, "?x=1&lat=") != nullptr);

  // Prázdná adresa a malý buffer se odmítnou.
  assert(!rainFeedBuildUrl("", 1.0f, 2.0f, 5, url, sizeof(url)));
  assert(!rainFeedBuildUrl(nullptr, 1.0f, 2.0f, 5, url, sizeof(url)));
  char tiny[10] = "";
  assert(!rainFeedBuildUrl("https://example.net/rain.json", 1.0f, 2.0f, 5, tiny,
                           sizeof(tiny)));

  printf("OK\n");
  return 0;
}
