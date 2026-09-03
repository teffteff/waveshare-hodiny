#include "RssParser.h"

#include <cstdint>
#include <cstring>

namespace {

// Přepisové tabulky: dva znaky na jeden kódový bod, \1 znamená "druhý znak
// není". Pokrývají celý blok, ne jen češtinu, protože ve zprávách běžně
// vystupují slovenská, polská nebo německá jména a stojí to stejně.
//
// Pořadí odpovídá U+00C0 až U+00FF.
const char LATIN1_SUPPLEMENT[] =
    "A\1A\1A\1A\1A\1A\1AEC\1"   // À Á Â Ã Ä Å Æ Ç
    "E\1E\1E\1E\1I\1I\1I\1I\1"  // È É Ê Ë Ì Í Î Ï
    "D\1N\1O\1O\1O\1O\1O\1x\1"  // Ð Ñ Ò Ó Ô Õ Ö ×
    "O\1U\1U\1U\1U\1Y\1THss"    // Ø Ù Ú Û Ü Ý Þ ß
    "a\1a\1a\1a\1a\1a\1aec\1"   // à á â ã ä å æ ç
    "e\1e\1e\1e\1i\1i\1i\1i\1"  // è é ê ë ì í î ï
    "d\1n\1o\1o\1o\1o\1o\1/\1"  // ð ñ ò ó ô õ ö ÷
    "o\1u\1u\1u\1u\1y\1thy\1";  // ø ù ú û ü ý þ ÿ

// Pořadí odpovídá U+0100 až U+017F. Čeština sedí uvnitř: Č je U+010C, Ř je
// U+0158, Ž je U+017D.
const char LATIN_EXTENDED_A[] =
    "A\1a\1A\1a\1A\1a\1C\1c\1"  // Ā ā Ă ă Ą ą Ć ć
    "C\1c\1C\1c\1C\1c\1D\1d\1"  // Ĉ ĉ Ċ ċ Č č Ď ď
    "D\1d\1E\1e\1E\1e\1E\1e\1"  // Đ đ Ē ē Ĕ ĕ Ė ė
    "E\1e\1E\1e\1G\1g\1G\1g\1"  // Ę ę Ě ě Ĝ ĝ Ğ ğ
    "G\1g\1G\1g\1H\1h\1H\1h\1"  // Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ
    "I\1i\1I\1i\1I\1i\1I\1i\1"  // Ĩ ĩ Ī ī Ĭ ĭ Į į
    "I\1i\1IJijJ\1j\1K\1k\1"    // İ ı Ĳ ĳ Ĵ ĵ Ķ ķ
    "k\1L\1l\1L\1l\1L\1l\1L\1"  // ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
    "l\1L\1l\1N\1n\1N\1n\1N\1"  // ŀ Ł ł Ń ń Ņ ņ Ň
    "n\1'nN\1n\1O\1o\1O\1o\1"   // ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
    "O\1o\1OEoeR\1r\1R\1r\1"    // Ő ő Œ œ Ŕ ŕ Ŗ ŗ
    "R\1r\1S\1s\1S\1s\1S\1s\1"  // Ř ř Ś ś Ŝ ŝ Ş ş
    "S\1s\1T\1t\1T\1t\1T\1t\1"  // Š š Ţ ţ Ť ť Ŧ ŧ
    "U\1u\1U\1u\1U\1u\1U\1u\1"  // Ũ ũ Ū ū Ŭ ŭ Ů ů
    "U\1u\1U\1u\1W\1w\1Y\1y\1"  // Ű ű Ų ų Ŵ ŵ Ŷ ŷ
    "Y\1Z\1z\1Z\1z\1Z\1z\1s\1"; // Ÿ Ź ź Ż ż Ž ž ſ

static_assert(sizeof(LATIN1_SUPPLEMENT) == 64 * 2 + 1,
              "Tabulka Latin-1 Supplement musí mít 64 dvouznakových položek.");
static_assert(sizeof(LATIN_EXTENDED_A) == 128 * 2 + 1,
              "Tabulka Latin Extended-A musí mít 128 dvouznakových položek.");

constexpr uint32_t CODE_POINT_SKIP = 0xFFFFFFFFu;

// Zapisuje přepsaný text a přitom slučuje bílé znaky. Mezera se drží jako
// odložená a zapíše se až před dalším skutečným znakem, takže odsazení na
// začátku ani na konci titulku nic nestojí.
class AsciiWriter {
 public:
  AsciiWriter(char *destination, size_t destinationSize)
      : destination_(destination), destinationSize_(destinationSize) {}

  void space() {
    if (length_ > 0) pendingSpace_ = true;
  }

  void append(const char *text, size_t textLength) {
    if (textLength == 0) return;
    if (pendingSpace_) {
      pendingSpace_ = false;
      putByte(' ');
    }
    for (size_t index = 0; index < textLength; ++index) putByte(text[index]);
  }

  void append(const char *text) { append(text, strlen(text)); }

  size_t finish() {
    if (destinationSize_ > 0) destination_[length_] = '\0';
    return length_;
  }

 private:
  void putByte(char value) {
    if (destinationSize_ == 0 || length_ + 1 >= destinationSize_) return;
    destination_[length_++] = value;
  }

  char *destination_;
  size_t destinationSize_;
  size_t length_ = 0;
  bool pendingSpace_ = false;
};

bool isXmlSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// Kódové body mimo ASCII, které projektová písma opravdu obsahují, takže se
// nemusí přepisovat. Odpovídá rozsahu v ClockCzechFont*.c.
const char *passThroughUtf8(uint32_t codePoint) {
  switch (codePoint) {
    case 0x00B0: return "\xC2\xB0";  // °
    case 0x00B3: return "\xC2\xB3";  // ³
    case 0x00B5: return "\xC2\xB5";  // µ
    default: return nullptr;
  }
}

// Interpunkce, kterou zpravodajské kanály používají a která nemá vlastní
// místo v přepisových tabulkách. iROZHLAS.cz sází uvozovky jako U+201A a
// U+2018, proto jsou tady i jednoduché varianty.
const char *punctuationAscii(uint32_t codePoint) {
  switch (codePoint) {
    case 0x00A9: return "(c)";
    case 0x00AB:
    case 0x00BB: return "\"";
    case 0x00AD:  // měkký dělicí spojovník
    case 0x00AE:
    case 0x200B:
    case 0x200C:
    case 0x200D:
    case 0x2060:
    case 0xFEFF:
    case 0x2122: return "";
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2212: return "-";
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032: return "'";
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x2039: return "<";
    case 0x203A: return ">";
    case 0x20AC: return "EUR";
    default: return nullptr;
  }
}

void writeCodePoint(AsciiWriter &writer, uint32_t codePoint) {
  if (codePoint == CODE_POINT_SKIP) return;
  if (codePoint == '\t' || codePoint == '\n' || codePoint == '\r' ||
      codePoint == ' ' || codePoint == 0x00A0 || codePoint == 0x2007 ||
      codePoint == 0x2009 || codePoint == 0x202F) {
    writer.space();
    return;
  }
  if (codePoint >= 0x20 && codePoint < 0x7F) {
    const char value = static_cast<char>(codePoint);
    writer.append(&value, 1);
    return;
  }
  const char *table = nullptr;
  size_t offset = 0;
  if (codePoint >= 0x00C0 && codePoint <= 0x00FF) {
    table = LATIN1_SUPPLEMENT;
    offset = codePoint - 0x00C0;
  } else if (codePoint >= 0x0100 && codePoint <= 0x017F) {
    table = LATIN_EXTENDED_A;
    offset = codePoint - 0x0100;
  }
  if (table != nullptr) {
    const char *entry = table + offset * 2;
    writer.append(entry, entry[1] == '\1' ? 1 : 2);
    return;
  }
  const char *passThrough = passThroughUtf8(codePoint);
  if (passThrough != nullptr) {
    writer.append(passThrough);
    return;
  }
  const char *punctuation = punctuationAscii(codePoint);
  if (punctuation != nullptr) {
    writer.append(punctuation);
    return;
  }
  // Cokoliv dalšího se zahodí. Prázdné místo je čitelnější než obdélníček,
  // který LVGL vykreslí za chybějící glyf.
}

// Pojmenované XML entity. Číselné tvary řeší volající, takže tabulka pokrývá
// jen to, co se v kanálech opravdu objevuje.
uint32_t namedEntity(const char *name, size_t length) {
  struct Entity {
    const char *name;
    uint32_t codePoint;
  };
  static const Entity ENTITIES[] = {
      {"amp", '&'},      {"lt", '<'},        {"gt", '>'},
      {"quot", '"'},     {"apos", '\''},     {"nbsp", 0x00A0},
      {"ndash", 0x2013}, {"mdash", 0x2014},  {"hellip", 0x2026},
      {"laquo", 0x00AB}, {"raquo", 0x00BB},  {"bdquo", 0x201E},
      {"ldquo", 0x201C}, {"rdquo", 0x201D},  {"lsquo", 0x2018},
      {"rsquo", 0x2019}, {"sbquo", 0x201A},  {"eacute", 0x00E9},
      {"aacute", 0x00E1}, {"iacute", 0x00ED}, {"oacute", 0x00F3},
      {"uacute", 0x00FA}, {"yacute", 0x00FD}, {"deg", 0x00B0},
      {"euro", 0x20AC},  {"middot", 0x00B7}, {"bull", 0x2022},
  };
  for (const Entity &entity : ENTITIES) {
    if (strlen(entity.name) == length &&
        strncmp(entity.name, name, length) == 0) {
      return entity.codePoint;
    }
  }
  return CODE_POINT_SKIP;
}

uint32_t parseHexDigit(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint32_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<uint32_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<uint32_t>(value - 'A' + 10);
  return CODE_POINT_SKIP;
}

// Přečte entitu začínající na position (ukazuje na '&'). Když to entita není,
// vrátí false a volající zpracuje '&' jako obyčejný znak.
bool readEntity(const char *source, size_t length, size_t position,
                uint32_t &codePoint, size_t &consumed) {
  size_t end = position + 1;
  while (end < length && end - position <= 12 && source[end] != ';') ++end;
  if (end >= length || source[end] != ';') return false;
  const char *name = source + position + 1;
  const size_t nameLength = end - position - 1;
  if (nameLength == 0) return false;
  consumed = end - position + 1;
  if (name[0] != '#') {
    codePoint = namedEntity(name, nameLength);
    return true;
  }
  uint32_t value = 0;
  const bool hexadecimal = nameLength > 1 && (name[1] == 'x' || name[1] == 'X');
  const size_t digitsBegin = hexadecimal ? 2 : 1;
  if (nameLength <= digitsBegin) return false;
  for (size_t index = digitsBegin; index < nameLength; ++index) {
    const uint32_t digit = hexadecimal
                               ? parseHexDigit(name[index])
                               : (name[index] >= '0' && name[index] <= '9'
                                      ? static_cast<uint32_t>(name[index] - '0')
                                      : CODE_POINT_SKIP);
    if (digit == CODE_POINT_SKIP) return false;
    value = value * (hexadecimal ? 16 : 10) + digit;
    if (value > 0x10FFFF) return false;
  }
  codePoint = value;
  return true;
}

// Dekóduje jeden znak UTF-8. Neplatná sekvence spotřebuje jeden bajt a vrátí
// CODE_POINT_SKIP, takže poškozený vstup nikdy nezacyklí smyčku.
uint32_t readUtf8(const char *source, size_t length, size_t position,
                  size_t &consumed) {
  const unsigned char lead = static_cast<unsigned char>(source[position]);
  size_t extra = 0;
  uint32_t value = 0;
  if (lead < 0x80) {
    consumed = 1;
    return lead;
  } else if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    value = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    value = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    value = lead & 0x07;
  } else {
    consumed = 1;
    return CODE_POINT_SKIP;
  }
  if (position + extra >= length) {
    consumed = 1;
    return CODE_POINT_SKIP;
  }
  for (size_t index = 1; index <= extra; ++index) {
    const unsigned char continuation =
        static_cast<unsigned char>(source[position + index]);
    if ((continuation & 0xC0) != 0x80) {
      consumed = 1;
      return CODE_POINT_SKIP;
    }
    value = (value << 6) | (continuation & 0x3F);
  }
  consumed = extra + 1;
  return value;
}

struct Range {
  size_t begin = 0;
  size_t end = 0;
};

bool nameMatches(const char *payload, size_t length, size_t position,
                 const char *name) {
  const size_t nameLength = strlen(name);
  if (position + nameLength > length) return false;
  if (strncmp(payload + position, name, nameLength) != 0) return false;
  const char next = position + nameLength < length
                        ? payload[position + nameLength]
                        : '\0';
  return next == '>' || next == '/' || isXmlSpace(next);
}

// Najde první výskyt elementu name v rozsahu [from, limit) a vrátí rozsah jeho
// obsahu. Značka smí nést atributy (<title type="text">) i být prázdná
// (<title/>). Jmenné prostory se ignorují, takže <dc:date> najde "date".
bool findElement(const char *payload, size_t limit, size_t from,
                 const char *name, Range &content, size_t &after) {
  for (size_t position = from; position + 1 < limit; ++position) {
    if (payload[position] != '<') continue;
    size_t nameBegin = position + 1;
    if (nameBegin < limit && payload[nameBegin] == '/') continue;
    if (nameBegin < limit && payload[nameBegin] == '!') continue;
    if (nameBegin < limit && payload[nameBegin] == '?') continue;
    // Přeskočí prefix jmenného prostoru, pokud za ním následuje hledané jméno.
    size_t scan = nameBegin;
    while (scan < limit && payload[scan] != '>' && payload[scan] != ':' &&
           !isXmlSpace(payload[scan]))
      ++scan;
    if (scan < limit && payload[scan] == ':') nameBegin = scan + 1;
    if (!nameMatches(payload, limit, nameBegin, name)) continue;

    size_t tagEnd = nameBegin;
    while (tagEnd < limit && payload[tagEnd] != '>') ++tagEnd;
    if (tagEnd >= limit) return false;
    if (payload[tagEnd - 1] == '/') {
      content.begin = tagEnd;
      content.end = tagEnd;
      after = tagEnd + 1;
      return true;
    }
    content.begin = tagEnd + 1;
    // Uzavírací značka se hledá podle holého jména, aby seděla i na prefix.
    for (size_t close = content.begin; close + 1 < limit; ++close) {
      if (payload[close] != '<' || payload[close + 1] != '/') continue;
      size_t closeName = close + 2;
      size_t closeScan = closeName;
      while (closeScan < limit && payload[closeScan] != '>' &&
             payload[closeScan] != ':' && !isXmlSpace(payload[closeScan]))
        ++closeScan;
      if (closeScan < limit && payload[closeScan] == ':')
        closeName = closeScan + 1;
      if (!nameMatches(payload, limit, closeName, name)) continue;
      content.end = close;
      after = closeScan;
      while (after < limit && payload[after] != '>') ++after;
      if (after < limit) ++after;
      return true;
    }
    return false;
  }
  return false;
}

// <![CDATA[...]]> se odloupne, aby se závorky nedostaly do titulku.
void stripCdata(const char *payload, Range &content) {
  static const char OPEN[] = "<![CDATA[";
  static const char CLOSE[] = "]]>";
  const size_t openLength = sizeof(OPEN) - 1;
  const size_t closeLength = sizeof(CLOSE) - 1;
  size_t begin = content.begin;
  size_t end = content.end;
  while (begin < end && isXmlSpace(payload[begin])) ++begin;
  while (end > begin && isXmlSpace(payload[end - 1])) --end;
  if (end - begin < openLength + closeLength) return;
  if (strncmp(payload + begin, OPEN, openLength) != 0) return;
  if (strncmp(payload + end - closeLength, CLOSE, closeLength) != 0) return;
  content.begin = begin + openLength;
  content.end = end - closeLength;
}

// Howard Hinnant, days_from_civil. Nezávislé na timegm, které hostitelský
// shim ani ESP-IDF nenabízejí ve stejné podobě.
int64_t daysFromCivil(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153u * (month + (month > 2 ? -3u : 9u)) + 2u) / 5u + day - 1u;
  const unsigned dayOfEra =
      yearOfEra * 365u + yearOfEra / 4u - yearOfEra / 100u + dayOfYear;
  return era * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

struct DateCursor {
  const char *text;
  size_t length;
  size_t position = 0;

  void skipSpace() {
    while (position < length && isXmlSpace(text[position])) ++position;
  }
  bool digits(unsigned &value, size_t minimum, size_t maximum) {
    size_t count = 0;
    value = 0;
    while (position < length && count < maximum && text[position] >= '0' &&
           text[position] <= '9') {
      value = value * 10 + static_cast<unsigned>(text[position] - '0');
      ++position;
      ++count;
    }
    return count >= minimum;
  }
};

bool monthFromName(const char *text, unsigned &month) {
  static const char NAMES[] = "janfebmaraprmayjunjulaugsepoctnovdec";
  char lowered[3];
  for (size_t index = 0; index < 3; ++index) {
    const char value = text[index];
    lowered[index] =
        value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value;
  }
  for (unsigned index = 0; index < 12; ++index) {
    if (strncmp(NAMES + index * 3, lowered, 3) == 0) {
      month = index + 1;
      return true;
    }
  }
  return false;
}

// Pojmenované zóny z RFC 822. Cokoliv neznámého se bere jako UTC, což je
// nejmenší možná chyba: čas se pak liší, ale zpráva se pořád zobrazí.
bool namedZoneOffset(const char *text, size_t length, int &offsetSeconds) {
  struct Zone {
    const char *name;
    int hours;
  };
  static const Zone ZONES[] = {
      {"UTC", 0},  {"GMT", 0},  {"UT", 0},   {"Z", 0},    {"EST", -5},
      {"EDT", -4}, {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6},
      {"PST", -8}, {"PDT", -7}, {"CET", 1},  {"CEST", 2},
  };
  for (const Zone &zone : ZONES) {
    const size_t nameLength = strlen(zone.name);
    if (nameLength == length && strncmp(zone.name, text, nameLength) == 0) {
      offsetSeconds = zone.hours * 3600;
      return true;
    }
  }
  return false;
}

bool composeUnix(int64_t year, unsigned month, unsigned day, unsigned hour,
                 unsigned minute, unsigned second, int offsetSeconds,
                 int64_t &unixSeconds) {
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60) {
    return false;
  }
  unixSeconds = daysFromCivil(year, month, day) * 86400 +
                static_cast<int64_t>(hour) * 3600 +
                static_cast<int64_t>(minute) * 60 +
                static_cast<int64_t>(second) - offsetSeconds;
  return true;
}

bool parseNumericZone(DateCursor &cursor, int &offsetSeconds) {
  if (cursor.position >= cursor.length) return false;
  const char sign = cursor.text[cursor.position];
  if (sign != '+' && sign != '-') return false;
  ++cursor.position;
  unsigned hours = 0;
  unsigned minutes = 0;
  if (!cursor.digits(hours, 2, 2)) return false;
  if (cursor.position < cursor.length && cursor.text[cursor.position] == ':')
    ++cursor.position;
  if (!cursor.digits(minutes, 2, 2)) minutes = 0;
  offsetSeconds = static_cast<int>(hours * 3600 + minutes * 60);
  if (sign == '-') offsetSeconds = -offsetSeconds;
  return true;
}

bool parseIso8601(DateCursor &cursor, int64_t &unixSeconds) {
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!cursor.digits(year, 4, 4)) return false;
  if (cursor.position >= cursor.length || cursor.text[cursor.position] != '-')
    return false;
  ++cursor.position;
  if (!cursor.digits(month, 2, 2)) return false;
  if (cursor.position >= cursor.length || cursor.text[cursor.position] != '-')
    return false;
  ++cursor.position;
  if (!cursor.digits(day, 2, 2)) return false;

  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  int offsetSeconds = 0;
  if (cursor.position < cursor.length &&
      (cursor.text[cursor.position] == 'T' ||
       cursor.text[cursor.position] == 't' ||
       isXmlSpace(cursor.text[cursor.position]))) {
    ++cursor.position;
    if (!cursor.digits(hour, 2, 2)) return false;
    if (cursor.position < cursor.length && cursor.text[cursor.position] == ':')
      ++cursor.position;
    if (!cursor.digits(minute, 2, 2)) return false;
    if (cursor.position < cursor.length && cursor.text[cursor.position] == ':') {
      ++cursor.position;
      if (!cursor.digits(second, 2, 2)) return false;
    }
    if (cursor.position < cursor.length && cursor.text[cursor.position] == '.') {
      ++cursor.position;
      unsigned fraction = 0;
      cursor.digits(fraction, 0, 9);
    }
    cursor.skipSpace();
    if (cursor.position < cursor.length) {
      const char zone = cursor.text[cursor.position];
      if (zone == 'Z' || zone == 'z') {
        offsetSeconds = 0;
      } else if (!parseNumericZone(cursor, offsetSeconds)) {
        offsetSeconds = 0;
      }
    }
  }
  return composeUnix(year, month, day, hour, minute, second, offsetSeconds,
                     unixSeconds);
}

bool parseRfc822(DateCursor &cursor, int64_t &unixSeconds) {
  cursor.skipSpace();
  // Volitelný název dne končí čárkou.
  size_t comma = cursor.position;
  while (comma < cursor.length && comma - cursor.position < 12 &&
         cursor.text[comma] != ',')
    ++comma;
  if (comma < cursor.length && cursor.text[comma] == ',')
    cursor.position = comma + 1;
  cursor.skipSpace();

  unsigned day = 0;
  if (!cursor.digits(day, 1, 2)) return false;
  cursor.skipSpace();
  if (cursor.position + 3 > cursor.length) return false;
  unsigned month = 0;
  if (!monthFromName(cursor.text + cursor.position, month)) return false;
  cursor.position += 3;
  while (cursor.position < cursor.length &&
         !isXmlSpace(cursor.text[cursor.position]))
    ++cursor.position;
  cursor.skipSpace();

  unsigned yearValue = 0;
  if (!cursor.digits(yearValue, 2, 4)) return false;
  int64_t year = yearValue;
  if (yearValue < 100) year = yearValue < 50 ? 2000 + yearValue : 1900 + yearValue;
  cursor.skipSpace();

  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  if (!cursor.digits(hour, 1, 2)) return false;
  if (cursor.position >= cursor.length || cursor.text[cursor.position] != ':')
    return false;
  ++cursor.position;
  if (!cursor.digits(minute, 2, 2)) return false;
  if (cursor.position < cursor.length && cursor.text[cursor.position] == ':') {
    ++cursor.position;
    if (!cursor.digits(second, 2, 2)) return false;
  }
  cursor.skipSpace();

  int offsetSeconds = 0;
  if (cursor.position < cursor.length) {
    if (!parseNumericZone(cursor, offsetSeconds)) {
      size_t zoneEnd = cursor.position;
      while (zoneEnd < cursor.length && !isXmlSpace(cursor.text[zoneEnd]))
        ++zoneEnd;
      namedZoneOffset(cursor.text + cursor.position,
                      zoneEnd - cursor.position, offsetSeconds);
    }
  }
  return composeUnix(year, month, day, hour, minute, second, offsetSeconds,
                     unixSeconds);
}

void copyError(char *error, size_t errorSize, const char *message) {
  if (error == nullptr || errorSize == 0) return;
  size_t length = strlen(message);
  if (length >= errorSize) length = errorSize - 1;
  memcpy(error, message, length);
  error[length] = '\0';
}

}  // namespace

size_t rssTransliterate(const char *source, size_t sourceLength,
                        char *destination, size_t destinationSize) {
  AsciiWriter writer(destination, destinationSize);
  if (source == nullptr) return writer.finish();
  size_t position = 0;
  while (position < sourceLength) {
    uint32_t codePoint = CODE_POINT_SKIP;
    size_t consumed = 1;
    if (source[position] == '&') {
      if (!readEntity(source, sourceLength, position, codePoint, consumed)) {
        codePoint = '&';
        consumed = 1;
      }
    } else {
      codePoint = readUtf8(source, sourceLength, position, consumed);
    }
    writeCodePoint(writer, codePoint);
    position += consumed;
  }
  return writer.finish();
}

bool rssParseDate(const char *text, size_t length, int64_t &unixSeconds) {
  if (text == nullptr || length == 0) return false;
  DateCursor cursor{text, length, 0};
  cursor.skipSpace();
  while (cursor.length > cursor.position &&
         isXmlSpace(cursor.text[cursor.length - 1]))
    --cursor.length;
  if (cursor.position >= cursor.length) return false;
  // ISO 8601 pozná podle pomlček na pevných pozicích, jinak je to RFC 822.
  const size_t remaining = cursor.length - cursor.position;
  const bool iso = remaining >= 10 && cursor.text[cursor.position + 4] == '-' &&
                   cursor.text[cursor.position + 7] == '-';
  return iso ? parseIso8601(cursor, unixSeconds)
             : parseRfc822(cursor, unixSeconds);
}

bool rssParseFeed(const char *payload, size_t length, size_t maximumItems,
                  RssFeed &feed, char *error, size_t errorSize) {
  feed = RssFeed{};
  copyError(error, errorSize, "");
  if (payload == nullptr || length == 0) {
    copyError(error, errorSize, "Kanál nevrátil žádná data.");
    return false;
  }
  size_t start = 0;
  while (start < length && isXmlSpace(payload[start])) ++start;
  if (start >= length || payload[start] != '<') {
    copyError(error, errorSize, "Adresa nevrací RSS ani Atom.");
    return false;
  }

  const size_t limit = maximumItems == 0 || maximumItems > RSS_MAX_ITEMS
                           ? RSS_MAX_ITEMS
                           : maximumItems;

  // Titulek kanálu se hledá jen před první zprávou, jinak by se sebral
  // titulek první položky.
  // RSS 2.0 i RDF používají <item>, Atom <entry>. Značka se určí jednou, aby
  // se u Atomu nehledal <item> znovu při každé zprávě.
  Range firstItem;
  size_t afterFirstItem = 0;
  const char *itemTag = "item";
  size_t channelLimit = length;
  bool haveFirstItem =
      findElement(payload, length, start, itemTag, firstItem, afterFirstItem);
  if (!haveFirstItem) {
    itemTag = "entry";
    haveFirstItem = findElement(payload, length, start, itemTag, firstItem,
                                afterFirstItem);
  }
  if (haveFirstItem) channelLimit = firstItem.begin;
  Range channelTitle;
  size_t afterChannelTitle = 0;
  if (findElement(payload, channelLimit, start, "title", channelTitle,
                  afterChannelTitle)) {
    stripCdata(payload, channelTitle);
    rssTransliterate(payload + channelTitle.begin,
                     channelTitle.end - channelTitle.begin, feed.channelTitle,
                     sizeof(feed.channelTitle));
  }

  // Řadicí klíč. Zprávy s datem se řadí od nejnovější; zprávy bez data se drží
  // v pořadí dokumentu a řadí se až za ně, protože kanály bývají seřazené
  // samy a první položka je pak ta nejnovější.
  constexpr int64_t UNDATED_BASE = INT64_MIN / 2;
  int64_t keys[RSS_MAX_ITEMS] = {};
  size_t documentIndex = 0;
  size_t position = start;
  while (position < length) {
    Range item;
    size_t after = 0;
    if (!findElement(payload, length, position, itemTag, item, after)) break;
    position = after > position ? after : position + 1;

    RssItem parsed;
    Range title;
    size_t afterTitle = 0;
    if (findElement(payload, item.end, item.begin, "title", title,
                    afterTitle)) {
      stripCdata(payload, title);
      rssTransliterate(payload + title.begin, title.end - title.begin,
                       parsed.title, sizeof(parsed.title));
    }
    if (parsed.title[0] == '\0') {
      ++documentIndex;
      continue;
    }

    Range date;
    size_t afterDate = 0;
    const bool hasDate =
        findElement(payload, item.end, item.begin, "pubDate", date,
                    afterDate) ||
        findElement(payload, item.end, item.begin, "published", date,
                    afterDate) ||
        findElement(payload, item.end, item.begin, "updated", date,
                    afterDate) ||
        findElement(payload, item.end, item.begin, "date", date, afterDate);
    if (hasDate) {
      stripCdata(payload, date);
      parsed.timeAvailable = rssParseDate(payload + date.begin,
                                          date.end - date.begin,
                                          parsed.publishedAt);
    }

    const int64_t key = parsed.timeAvailable
                            ? parsed.publishedAt
                            : UNDATED_BASE - static_cast<int64_t>(documentIndex);
    ++documentIndex;

    size_t slot = 0;
    while (slot < feed.count && keys[slot] >= key) ++slot;
    if (slot >= limit) continue;
    const size_t last = feed.count < limit ? feed.count : limit - 1;
    for (size_t index = last; index > slot; --index) {
      feed.items[index] = feed.items[index - 1];
      keys[index] = keys[index - 1];
    }
    feed.items[slot] = parsed;
    keys[slot] = key;
    if (feed.count < limit) ++feed.count;
  }

  if (feed.count == 0) {
    copyError(error, errorSize, "Kanál neobsahuje žádné zprávy.");
    return false;
  }
  return true;
}
