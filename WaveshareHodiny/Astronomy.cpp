#include "Astronomy.h"

#include <cmath>

namespace {
constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double DEG = PI_VALUE / 180.0;
// Refrakce 34' a poloměr slunečního kotouče 16'.
constexpr double SUN_HORIZON_DEG = -0.833;
// Krok hledání. Mezi východem a západem uplynou hodiny, takže deset minut
// žádný průchod obzorem nepřeskočí, a přesný okamžik se pak dohledá půlením.
constexpr int64_t SEARCH_STEP_SECONDS = 10 * 60;
constexpr int BISECTION_STEPS = 12;

double normalizeDegrees(double value) {
  value = std::fmod(value, 360.0);
  return value < 0.0 ? value + 360.0 : value;
}

double sinDeg(double value) { return std::sin(value * DEG); }
double cosDeg(double value) { return std::cos(value * DEG); }

// Dny od 0. ledna 2000 0:00 UT, jak je Schlyter používá.
double schlyterDay(int64_t epoch) {
  return static_cast<double>(epoch) / 86400.0 + 2440587.5 - 2451543.5;
}

struct EclipticPosition {
  double longitudeDeg = 0.0;
  double latitudeDeg = 0.0;
  // U Slunce v astronomických jednotkách, u Měsíce v zemských poloměrech.
  double distance = 0.0;
};

struct SunElements {
  double meanAnomalyDeg;
  double perihelionDeg;
};

SunElements sunElements(double day) {
  return {normalizeDegrees(356.0470 + 0.9856002585 * day),
          282.9404 + 4.70935e-5 * day};
}

EclipticPosition sunPosition(double day) {
  const SunElements elements = sunElements(day);
  constexpr double ECCENTRICITY_BASE = 0.016709;
  const double eccentricity = ECCENTRICITY_BASE - 1.151e-9 * day;
  const double meanAnomaly = elements.meanAnomalyDeg;
  // Oběžná dráha Země je skoro kruhová, první přiblížení Keplerovy rovnice
  // tu stačí.
  const double eccentricAnomaly =
      meanAnomaly + eccentricity / DEG * sinDeg(meanAnomaly) *
                        (1.0 + eccentricity * cosDeg(meanAnomaly));
  const double x = cosDeg(eccentricAnomaly) - eccentricity;
  const double y =
      std::sqrt(1.0 - eccentricity * eccentricity) * sinDeg(eccentricAnomaly);
  EclipticPosition position;
  position.longitudeDeg =
      normalizeDegrees(std::atan2(y, x) / DEG + elements.perihelionDeg);
  position.distance = std::sqrt(x * x + y * y);
  return position;
}

EclipticPosition moonPosition(double day) {
  const double node = normalizeDegrees(125.1228 - 0.0529538083 * day);
  constexpr double INCLINATION = 5.1454;
  const double perigee = normalizeDegrees(318.0634 + 0.1643573223 * day);
  constexpr double SEMI_MAJOR_AXIS = 60.2666;
  constexpr double ECCENTRICITY = 0.054900;
  const double meanAnomaly = normalizeDegrees(115.3654 + 13.0649929509 * day);

  // Měsíční dráha je výstřednější, Keplerova rovnice se proto dořeší
  // Newtonovou metodou.
  double eccentricAnomaly =
      meanAnomaly + ECCENTRICITY / DEG * sinDeg(meanAnomaly) *
                        (1.0 + ECCENTRICITY * cosDeg(meanAnomaly));
  for (int iteration = 0; iteration < 4; ++iteration) {
    eccentricAnomaly -=
        (eccentricAnomaly - ECCENTRICITY / DEG * sinDeg(eccentricAnomaly) -
         meanAnomaly) /
        (1.0 - ECCENTRICITY * cosDeg(eccentricAnomaly));
  }
  const double orbitX = SEMI_MAJOR_AXIS * (cosDeg(eccentricAnomaly) - ECCENTRICITY);
  const double orbitY = SEMI_MAJOR_AXIS *
                        std::sqrt(1.0 - ECCENTRICITY * ECCENTRICITY) *
                        sinDeg(eccentricAnomaly);
  const double trueAnomaly = std::atan2(orbitY, orbitX) / DEG;
  double distance = std::sqrt(orbitX * orbitX + orbitY * orbitY);

  const double argument = trueAnomaly + perigee;
  const double x = distance * (cosDeg(node) * cosDeg(argument) -
                               sinDeg(node) * sinDeg(argument) *
                                   cosDeg(INCLINATION));
  const double y = distance * (sinDeg(node) * cosDeg(argument) +
                               cosDeg(node) * sinDeg(argument) *
                                   cosDeg(INCLINATION));
  const double z = distance * sinDeg(argument) * sinDeg(INCLINATION);
  double longitude = std::atan2(y, x) / DEG;
  double latitude = std::atan2(z, std::sqrt(x * x + y * y)) / DEG;

  // Hlavní poruchy od Slunce. Bez nich by Měsíc ujel až o stupeň, tedy
  // o čtyři minuty ve východu.
  const SunElements sun = sunElements(day);
  const double sunMeanLongitude = sun.meanAnomalyDeg + sun.perihelionDeg;
  const double moonMeanLongitude = node + perigee + meanAnomaly;
  const double elongation = moonMeanLongitude - sunMeanLongitude;
  const double latitudeArgument = moonMeanLongitude - node;
  const double Mm = meanAnomaly;
  const double Ms = sun.meanAnomalyDeg;
  const double D = elongation;
  const double F = latitudeArgument;
  longitude += -1.274 * sinDeg(Mm - 2 * D) + 0.658 * sinDeg(2 * D) -
               0.186 * sinDeg(Ms) - 0.059 * sinDeg(2 * Mm - 2 * D) -
               0.057 * sinDeg(Mm - 2 * D + Ms) + 0.053 * sinDeg(Mm + 2 * D) +
               0.046 * sinDeg(2 * D - Ms) + 0.041 * sinDeg(Mm - Ms) -
               0.035 * sinDeg(D) - 0.031 * sinDeg(Mm + Ms) -
               0.015 * sinDeg(2 * F - 2 * D) + 0.011 * sinDeg(Mm - 4 * D);
  latitude += -0.173 * sinDeg(F - 2 * D) - 0.055 * sinDeg(Mm - F - 2 * D) -
              0.046 * sinDeg(Mm + F - 2 * D) + 0.033 * sinDeg(F + 2 * D) +
              0.017 * sinDeg(2 * Mm + F);
  distance += -0.58 * cosDeg(Mm - 2 * D) - 0.46 * cosDeg(2 * D);

  EclipticPosition position;
  position.longitudeDeg = normalizeDegrees(longitude);
  position.latitudeDeg = latitude;
  position.distance = distance;
  return position;
}

void eclipticToEquatorial(const EclipticPosition &position, double day,
                          double &rightAscensionDeg, double &declinationDeg) {
  const double obliquity = 23.4393 - 3.563e-7 * day;
  const double x = cosDeg(position.longitudeDeg) * cosDeg(position.latitudeDeg);
  const double y = sinDeg(position.longitudeDeg) * cosDeg(position.latitudeDeg);
  const double z = sinDeg(position.latitudeDeg);
  const double equatorialY = y * cosDeg(obliquity) - z * sinDeg(obliquity);
  const double equatorialZ = y * sinDeg(obliquity) + z * cosDeg(obliquity);
  rightAscensionDeg = normalizeDegrees(std::atan2(equatorialY, x) / DEG);
  declinationDeg =
      std::atan2(equatorialZ, std::sqrt(x * x + equatorialY * equatorialY)) /
      DEG;
}

double altitudeFor(const EclipticPosition &position, int64_t epoch,
                   double latitudeDeg, double longitudeDeg) {
  const double day = schlyterDay(epoch);
  double rightAscension = 0.0;
  double declination = 0.0;
  eclipticToEquatorial(position, day, rightAscension, declination);
  const double daysSinceJ2000 =
      static_cast<double>(epoch) / 86400.0 + 2440587.5 - 2451545.0;
  const double siderealDeg =
      normalizeDegrees(280.46061837 + 360.98564736629 * daysSinceJ2000);
  const double hourAngle = siderealDeg + longitudeDeg - rightAscension;
  const double sinAltitude =
      sinDeg(latitudeDeg) * sinDeg(declination) +
      cosDeg(latitudeDeg) * cosDeg(declination) * cosDeg(hourAngle);
  return std::asin(sinAltitude < -1.0 ? -1.0
                                      : (sinAltitude > 1.0 ? 1.0 : sinAltitude)) /
         DEG;
}
}  // namespace

double astronomyAltitudeDeg(AstronomyBody body, int64_t epoch,
                            double latitudeDeg, double longitudeDeg) {
  const double day = schlyterDay(epoch);
  const EclipticPosition position =
      body == AstronomyBody::Sun ? sunPosition(day) : moonPosition(day);
  return altitudeFor(position, epoch, latitudeDeg, longitudeDeg);
}

double astronomyHorizonMarginDeg(AstronomyBody body, int64_t epoch,
                                 double latitudeDeg, double longitudeDeg) {
  const double day = schlyterDay(epoch);
  if (body == AstronomyBody::Sun) {
    return altitudeFor(sunPosition(day), epoch, latitudeDeg, longitudeDeg) -
           SUN_HORIZON_DEG;
  }
  const EclipticPosition moon = moonPosition(day);
  // Paralaxa Měsíce je skoro stupeň a z geocentrické výšky ji odečítá
  // pozorovatel na povrchu. Horizont pro východ je proto 0,7275 paralaxy
  // mínus refrakce (34').
  const double parallaxDeg = std::asin(1.0 / moon.distance) / DEG;
  const double horizon = 0.7275 * parallaxDeg - 34.0 / 60.0;
  return altitudeFor(moon, epoch, latitudeDeg, longitudeDeg) - horizon;
}

namespace {
// Nejbližší průchod funkce margin nulou zdola nahoru (rising) nebo shora dolů.
// Hledá se po krocích a přesný okamžik se dohledá půlením.
template <typename Margin>
bool findCrossing(const Margin &margin, bool rising, int64_t from,
                  int64_t searchSeconds, int64_t &event) {
  const int64_t direction = searchSeconds < 0 ? -1 : 1;
  const int64_t limit = searchSeconds < 0 ? -searchSeconds : searchSeconds;
  int64_t previousTime = from;
  double previousMargin = margin(previousTime);
  for (int64_t offset = SEARCH_STEP_SECONDS; offset < limit + SEARCH_STEP_SECONDS;
       offset += SEARCH_STEP_SECONDS) {
    const int64_t step = offset > limit ? limit : offset;
    const int64_t currentTime = from + direction * step;
    const double currentMargin = margin(currentTime);
    // Časově dřívější a pozdější z dvojice, ať se hledá kterýmkoli směrem.
    const int64_t earlierTime = direction > 0 ? previousTime : currentTime;
    const int64_t laterTime = direction > 0 ? currentTime : previousTime;
    const double earlierMargin = direction > 0 ? previousMargin : currentMargin;
    const double laterMargin = direction > 0 ? currentMargin : previousMargin;
    const bool crossed = rising ? earlierMargin < 0.0 && laterMargin >= 0.0
                                : earlierMargin >= 0.0 && laterMargin < 0.0;
    if (crossed) {
      int64_t low = earlierTime;
      int64_t high = laterTime;
      for (int bisection = 0; bisection < BISECTION_STEPS && high - low > 1;
           ++bisection) {
        const int64_t middle = low + (high - low) / 2;
        const bool above = margin(middle) >= 0.0;
        // U vzestupu leží hledaný okamžik za posledním bodem pod nulou,
        // u sestupu za posledním bodem nad ní.
        if (above != rising)
          low = middle;
        else
          high = middle;
      }
      event = low + (high - low) / 2;
      return true;
    }
    previousTime = currentTime;
    previousMargin = currentMargin;
    if (step == limit) break;
  }
  return false;
}
}  // namespace

bool astronomyFindEvent(AstronomyBody body, bool rising, int64_t from,
                        int64_t searchSeconds, double latitudeDeg,
                        double longitudeDeg, int64_t &event) {
  return findCrossing(
      [&](int64_t epoch) {
        return astronomyHorizonMarginDeg(body, epoch, latitudeDeg,
                                         longitudeDeg);
      },
      rising, from, searchSeconds, event);
}

bool astronomyFindSunAltitude(double altitudeDeg, bool rising, int64_t from,
                              int64_t searchSeconds, double latitudeDeg,
                              double longitudeDeg, int64_t &event) {
  return findCrossing(
      [&](int64_t epoch) {
        return astronomyAltitudeDeg(AstronomyBody::Sun, epoch, latitudeDeg,
                                    longitudeDeg) -
               altitudeDeg;
      },
      rising, from, searchSeconds, event);
}

AstronomyMoonPhase astronomyMoonPhase(int64_t epoch) {
  const double day = schlyterDay(epoch);
  const EclipticPosition sun = sunPosition(day);
  const EclipticPosition moon = moonPosition(day);
  const double elongation =
      normalizeDegrees(moon.longitudeDeg - sun.longitudeDeg);
  AstronomyMoonPhase result;
  result.phase = elongation / 360.0;
  result.illuminationPercent = 50.0 * (1.0 - cosDeg(elongation));
  return result;
}

bool astronomyFindMoonPhase(double targetPhase, int64_t from,
                            int64_t searchSeconds, int64_t &event) {
  // Odchylka fáze od cíle ve stupních, v rozsahu -180 až 180. Při průchodu
  // cílem přeskočí ze záporné na kladnou; skok z +180 na -180 na opačné straně
  // lunace se pozná podle velikosti.
  const auto offset = [targetPhase](int64_t epoch) {
    double degrees = astronomyMoonPhase(epoch).phase * 360.0 -
                     targetPhase * 360.0;
    degrees = normalizeDegrees(degrees);
    return degrees > 180.0 ? degrees - 360.0 : degrees;
  };
  constexpr int64_t STEP = 6 * 60 * 60;
  int64_t previousTime = from;
  double previous = offset(previousTime);
  for (int64_t elapsed = STEP; elapsed <= searchSeconds; elapsed += STEP) {
    const int64_t currentTime = from + elapsed;
    const double current = offset(currentTime);
    if (previous < 0.0 && current >= 0.0 && current - previous < 90.0) {
      int64_t low = previousTime;
      int64_t high = currentTime;
      while (high - low > 30) {
        const int64_t middle = low + (high - low) / 2;
        if (offset(middle) < 0.0)
          low = middle;
        else
          high = middle;
      }
      event = low + (high - low) / 2;
      return true;
    }
    previousTime = currentTime;
    previous = current;
  }
  return false;
}

uint8_t astronomyMoonPhaseIndex(double phase) {
  phase -= std::floor(phase);
  return static_cast<uint8_t>(static_cast<int>(std::floor(phase * 8.0 + 0.5)) % 8);
}
