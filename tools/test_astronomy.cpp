#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../WaveshareHodiny/Astronomy.h"

// Referenční časy spočítal PyEphem 4.2 s konvencí USNO: horní okraj kotouče,
// obzor -0:34, bez atmosféry v modelu, topocentricky. Začátek hledání je
// půlnoc UTC daného dne; -1 znamená, že do 36 hodin žádná událost není.
namespace {
struct EventCase {
  const char *place;
  double latitude;
  double longitude;
  AstronomyBody body;
  bool rising;
  int64_t from;
  int64_t expected;
};

const EventCase EVENT_CASES[] = {
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1768435200LL, 1768459988LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1768435200LL, 1768490865LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1768435200LL, 1768452395LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1768435200LL, 1768477783LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1773964800LL, 1773983039LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1773964800LL, 1774026816LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1773964800LL, 1773983932LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1773964800LL, 1774034892LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1782000000LL, 1782010323LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1782000000LL, 1782069198LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1782000000LL, 1782038266LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1782000000LL, 1782081995LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1789257600LL, 1789274019LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1789257600LL, 1789319939LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1789257600LL, 1789284066LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1789257600LL, 1789321509LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1797811200LL, 1797836174LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1797811200LL, 1797865294LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1797811200LL, 1797855379LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1797811200LL, 1797825346LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, true, 1806624000LL, 1806640580LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Sun, false, 1806624000LL, 1806687220LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, true, 1806624000LL, 1806634534LL},
  {"Ondrejov", 49.90461, 14.7842, AstronomyBody::Moon, false, 1806624000LL, 1806669438LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1768435200LL, 1768459215LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1768435200LL, 1768490874LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1768435200LL, 1768451345LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1768435200LL, 1768478049LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1773964800LL, 1773982665LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1773964800LL, 1774026423LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1773964800LL, 1773983693LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1773964800LL, 1774034261LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1782000000LL, 1782010437LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1782000000LL, 1782068320LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1782000000LL, 1782037871LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1782000000LL, 1782081655LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1789257600LL, 1789273711LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1789257600LL, 1789319488LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1789257600LL, 1789283475LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1789257600LL, 1789321351LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1797811200LL, 1797835338LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1797811200LL, 1797865367LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1797811200LL, 1797855477LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1797811200LL, 1797824479LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, true, 1806624000LL, 1806640290LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Sun, false, 1806624000LL, 1806686744LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, true, 1806624000LL, 1806633844LL},
  {"Vienna", 48.2082, 16.3738, AstronomyBody::Moon, false, 1806624000LL, 1806669310LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1768435200LL, 1768472945LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1768435200LL, 1768475925LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1768435200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1768435200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1773964800LL, 1773981835LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1773964800LL, 1774026092LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1773964800LL, 1773979740LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1773964800LL, 1774039374LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1782000000LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1782000000LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1782000000LL, 1782037069LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1782000000LL, 1782080019LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1789257600LL, 1789271349LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1789257600LL, 1789320522LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1789257600LL, 1789287502LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1789257600LL, 1789315259LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1797811200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1797811200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1797811200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1797811200LL, -1LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, true, 1806624000LL, 1806637530LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Sun, false, 1806624000LL, 1806688362LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, true, 1806624000LL, 1806641135LL},
  {"Tromso", 69.6492, 18.9553, AstronomyBody::Moon, false, 1806624000LL, 1806661348LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1768435200LL, 1768503625LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1768435200LL, 1768468140LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1768435200LL, 1768493191LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1768435200LL, 1768458728LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1773964800LL, 1774036723LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1773964800LL, 1773994017LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1773964800LL, 1774044287LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1773964800LL, 1773995628LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1782000000LL, 1782075612LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1782000000LL, 1782024827LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1782000000LL, 1782005099LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1782000000LL, 1782049133LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1789257600LL, 1789329391LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1789257600LL, 1789285506LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1789257600LL, 1789333954LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1789257600LL, 1789293508LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1797811200LL, 1797878466LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1797811200LL, 1797843926LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1797811200LL, 1797833151LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1797811200LL, 1797869580LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, true, 1806624000LL, 1806696501LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Sun, false, 1806624000LL, 1806652183LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, true, 1806624000LL, 1806682368LL},
  {"Sydney", -33.8688, 151.2093, AstronomyBody::Moon, false, 1806624000LL, 1806642906LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1768435200LL, 1768475964LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1768435200LL, 1768519638LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1768435200LL, 1768465981LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1768435200LL, 1768510733LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1773964800LL, 1774005480LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1773964800LL, 1774049070LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1773964800LL, 1774009671LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1773964800LL, 1773964904LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1782000000LL, 1782040344LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1782000000LL, 1782083945LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1782000000LL, 1782061415LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1782000000LL, 1782016857LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1789257600LL, 1789297593LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1789257600LL, 1789341175LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1789257600LL, 1789303999LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1789257600LL, 1789259401LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1797811200LL, 1797851278LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1797811200LL, 1797894968LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1797811200LL, 1797886541LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1797811200LL, 1797841384LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, true, 1806624000LL, 1806664455LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Sun, false, 1806624000LL, 1806708038LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, true, 1806624000LL, 1806652486LL},
  {"Quito", -0.18, -78.47, AstronomyBody::Moon, false, 1806624000LL, 1806696950LL},
};

struct PhaseCase {
  int64_t epoch;
  double illuminationPercent;
};

const PhaseCase PHASE_CASES[] = {
  {1767398400LL, 99.60},
  {1769947200LL, 99.72},
  {1780185600LL, 99.69},
  {1789329600LL, 8.35},
  {1792972800LL, 99.81},
  {1805068800LL, 42.49},
};

constexpr int64_t SEARCH_SECONDS = 36 * 60 * 60;
// Slunce drží Schlyterova dráha na úhlovou minutu, Měsíc s hlavními poruchami
// na několik minut; u Tromsø se obzorem plazí šikmo, takže chyba roste.
constexpr int64_t SUN_TOLERANCE_SECONDS = 120;
constexpr int64_t MOON_TOLERANCE_SECONDS = 300;
constexpr double POLAR_LATITUDE = 66.0;
}  // namespace

int main() {
  int failures = 0;
  int64_t worstSun = 0;
  int64_t worstMoon = 0;
  for (const EventCase &test : EVENT_CASES) {
    int64_t event = 0;
    const bool found = astronomyFindEvent(test.body, test.rising, test.from,
                                          SEARCH_SECONDS, test.latitude,
                                          test.longitude, event);
    if (test.expected < 0) {
      if (found) {
        std::printf("%s %s %s: nečekaná událost %lld\n", test.place,
                    test.body == AstronomyBody::Sun ? "slunce" : "měsíc",
                    test.rising ? "východ" : "západ",
                    static_cast<long long>(event));
        ++failures;
      }
      continue;
    }
    const int64_t error = found ? std::llabs(event - test.expected) : -1;
    const bool sun = test.body == AstronomyBody::Sun;
    int64_t tolerance = sun ? SUN_TOLERANCE_SECONDS : MOON_TOLERANCE_SECONDS;
    if (std::fabs(test.latitude) > POLAR_LATITUDE) tolerance *= 3;
    if (!found || error > tolerance) {
      std::printf("%s %s %s od %lld: čekáno %lld, vyšlo %lld (chyba %lld s)\n",
                  test.place, sun ? "slunce" : "měsíc",
                  test.rising ? "východ" : "západ",
                  static_cast<long long>(test.from),
                  static_cast<long long>(test.expected),
                  static_cast<long long>(found ? event : -1),
                  static_cast<long long>(error));
      ++failures;
      continue;
    }
    if (std::fabs(test.latitude) <= POLAR_LATITUDE) {
      if (sun && error > worstSun) worstSun = error;
      if (!sun && error > worstMoon) worstMoon = error;
    }
  }

  for (const PhaseCase &test : PHASE_CASES) {
    const AstronomyMoonPhase phase = astronomyMoonPhase(test.epoch);
    if (std::fabs(phase.illuminationPercent - test.illuminationPercent) > 2.0) {
      std::printf("fáze %lld: čekáno %.1f %%, vyšlo %.1f %%\n",
                  static_cast<long long>(test.epoch), test.illuminationPercent,
                  phase.illuminationPercent);
      ++failures;
    }
  }

  // Občanské svítání a soumrak: střed Slunce 6° pod obzorem, PyEphem s
  // use_center=True a bez atmosféry. V Tromsø v červnu soumrak nenastane.
  struct TwilightCase {
    const char *place;
    double latitude;
    double longitude;
    bool rising;
    int64_t from;
    int64_t expected;
  };
  const TwilightCase TWILIGHT_CASES[] = {
      {"Ondrejov", 49.90461, 14.7842, true, 1768435200LL, 1768457780LL},
      {"Ondrejov", 49.90461, 14.7842, false, 1768435200LL, 1768493074LL},
      {"Ondrejov", 49.90461, 14.7842, true, 1782000000LL, 1782007651LL},
      {"Ondrejov", 49.90461, 14.7842, false, 1782000000LL, 1782071869LL},
      {"Ondrejov", 49.90461, 14.7842, true, 1789344000LL, 1789358553LL},
      {"Ondrejov", 49.90461, 14.7842, false, 1789344000LL, 1789408156LL},
      {"Ondrejov", 49.90461, 14.7842, true, 1797811200LL, 1797833871LL},
      {"Ondrejov", 49.90461, 14.7842, false, 1797811200LL, 1797867598LL},
      {"Tromso", 69.6492, 18.9553, true, 1768435200LL, 1768463829LL},
      {"Tromso", 69.6492, 18.9553, false, 1768435200LL, 1768485044LL},
      {"Tromso", 69.6492, 18.9553, true, 1782000000LL, -1LL},
      {"Tromso", 69.6492, 18.9553, false, 1782000000LL, -1LL},
      {"Tromso", 69.6492, 18.9553, true, 1789344000LL, 1789354174LL},
      {"Tromso", 69.6492, 18.9553, false, 1789344000LL, 1789410415LL},
      {"Sydney", -33.8688, 151.2093, true, 1768435200LL, 1768501930LL},
      {"Sydney", -33.8688, 151.2093, false, 1768435200LL, 1768469835LL},
      {"Sydney", -33.8688, 151.2093, true, 1789344000LL, 1789414214LL},
      {"Sydney", -33.8688, 151.2093, false, 1789344000LL, 1789373442LL},
      {"Quito", -0.18, -78.47, true, 1768435200LL, 1768474636LL},
      {"Quito", -0.18, -78.47, false, 1768435200LL, 1768520965LL},
      {"Quito", -0.18, -78.47, true, 1797811200LL, 1797849927LL},
      {"Quito", -0.18, -78.47, false, 1797811200LL, 1797896319LL},
  };
  int64_t worstTwilight = 0;
  for (const TwilightCase &test : TWILIGHT_CASES) {
    int64_t event = 0;
    const bool found = astronomyFindSunAltitude(
        ASTRONOMY_CIVIL_TWILIGHT_DEG, test.rising, test.from, SEARCH_SECONDS,
        test.latitude, test.longitude, event);
    const int64_t tolerance =
        std::fabs(test.latitude) > POLAR_LATITUDE ? 3 * SUN_TOLERANCE_SECONDS
                                                  : SUN_TOLERANCE_SECONDS;
    const bool ok = test.expected < 0
                        ? !found
                        : found && std::llabs(event - test.expected) <= tolerance;
    if (!ok) {
      std::printf("%s soumrak %s od %lld: čekáno %lld, vyšlo %lld\n",
                  test.place, test.rising ? "ranní" : "večerní",
                  static_cast<long long>(test.from),
                  static_cast<long long>(test.expected),
                  static_cast<long long>(found ? event : -1));
      ++failures;
    } else if (found && std::fabs(test.latitude) <= POLAR_LATITUDE &&
               std::llabs(event - test.expected) > worstTwilight) {
      worstTwilight = std::llabs(event - test.expected);
    }
  }
  std::printf("nejhorší odchylka občanského soumraku: %lld s\n",
              static_cast<long long>(worstTwilight));

  // Příští úplněk a nov podle PyEphem. Schlyterův Měsíc s poruchami drží
  // fázi na několik minut.
  struct MoonEventCase {
    int64_t from;
    int64_t fullMoon;
    int64_t newMoon;
  };
  constexpr MoonEventCase MOON_EVENTS[] = {
      {1767225600LL, 1767434570LL, 1768765914LL},
      {1789329600LL, 1790441337LL, 1791647401LL},
      {1802217600LL, 1803165814LL, 1804498164LL},
  };
  constexpr int64_t MOON_PHASE_TOLERANCE_SECONDS = 30 * 60;
  for (const MoonEventCase &test : MOON_EVENTS) {
    int64_t fullMoon = 0;
    int64_t newMoon = 0;
    assert(astronomyFindMoonPhase(0.5, test.from, 31LL * 86400, fullMoon));
    assert(astronomyFindMoonPhase(0.0, test.from, 31LL * 86400, newMoon));
    if (std::llabs(fullMoon - test.fullMoon) > MOON_PHASE_TOLERANCE_SECONDS ||
        std::llabs(newMoon - test.newMoon) > MOON_PHASE_TOLERANCE_SECONDS) {
      std::printf("fáze od %lld: úplněk %lld (čekáno %lld), nov %lld (čekáno %lld)\n",
                  static_cast<long long>(test.from),
                  static_cast<long long>(fullMoon),
                  static_cast<long long>(test.fullMoon),
                  static_cast<long long>(newMoon),
                  static_cast<long long>(test.newMoon));
      ++failures;
    }
  }

  // Pojmenované fáze: nov kolem nuly z obou stran, úplněk v půlce.
  assert(astronomyMoonPhaseIndex(0.0) == 0);
  assert(astronomyMoonPhaseIndex(0.98) == 0);
  assert(astronomyMoonPhaseIndex(0.25) == 2);
  assert(astronomyMoonPhaseIndex(0.5) == 4);
  assert(astronomyMoonPhaseIndex(0.74) == 6);
  assert(astronomyMoonPhaseIndex(1.0) == 0);

  // Hledání zpět vrátí stejný západ, jaký hledání vpřed našlo.
  int64_t sunset = 0;
  assert(astronomyFindEvent(AstronomyBody::Sun, false, 1789286400LL,
                            SEARCH_SECONDS, 49.90461, 14.7842, sunset));
  int64_t previousSunset = 0;
  assert(astronomyFindEvent(AstronomyBody::Sun, false, sunset + 3600,
                            -SEARCH_SECONDS, 49.90461, 14.7842,
                            previousSunset));
  assert(std::llabs(previousSunset - sunset) <= 2);

  std::printf("nejhorší odchylka mimo polární oblast: slunce %lld s, měsíc %lld s\n",
              static_cast<long long>(worstSun), static_cast<long long>(worstMoon));
  return failures == 0 ? 0 : 1;
}
