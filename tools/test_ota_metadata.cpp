#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../WaveshareHodiny/Board.h"
#include "../WaveshareHodiny/OtaMetadata.h"

namespace {

constexpr char SERVER[] = "https://teffteff.github.io";
constexpr char SHA[] =
    "83f41f44988493f0ceddea4864cd9e646946a0ddb0d108fe450e192a4a607839";

OtaMetadataResult parse(const std::string &json, const char *board,
                        OtaMetadata &out) {
  return otaParseMetadata(json.data(), json.size(), SERVER, "ESP32-S3", board,
                          out);
}

// Metadata ve tvaru, jak je píše tools/prepare_release_package.py.
std::string metadata(const char *board, const char *url,
                     const char *size = "3253248",
                     const char *sha = SHA) {
  std::string json = "{\n  \"version\": \"2.5.0\",\n  \"chipFamily\": \"ESP32-S3\",\n";
  if (board != nullptr) json += std::string("  \"board\": \"") + board + "\",\n";
  json += std::string("  \"size\": ") + size + ",\n  \"sha256\": \"" + sha +
          "\",\n  \"url\": \"" + url + "\"\n}\n";
  return json;
}

void testMetadataUrl() {
  char url[200];
  assert(otaMetadataUrl(SERVER, "/waveshare-hodiny/firmware/ota.json",
                        "waveshare-hodiny", "lcd21", url, sizeof(url)));
  assert(strcmp(url, "https://teffteff.github.io/waveshare-hodiny/firmware/ota.json") == 0);
  assert(otaMetadataUrl(SERVER, "/waveshare-hodiny/firmware/ota.json",
                        "waveshare-hodiny", "lcd7", url, sizeof(url)));
  assert(strcmp(url, "https://teffteff.github.io/waveshare-hodiny/firmware/ota-lcd7.json") == 0);
  // Jiná cesta než .../ota.json se nemění ani pro 7".
  assert(otaMetadataUrl(SERVER, "/x/meta.json", "slug", "lcd7", url, sizeof(url)));
  assert(strcmp(url, "https://teffteff.github.io/x/meta.json") == 0);
  // Interní Firmware Hub.
  assert(otaMetadataUrl("https://hub.example", "", "waveshare-hodiny", "lcd21",
                        url, sizeof(url)));
  assert(strcmp(url, "https://hub.example/api/v1/projects/waveshare-hodiny/ota") == 0);
  assert(otaMetadataUrl("https://hub.example", nullptr, "waveshare-hodiny",
                        "lcd7", url, sizeof(url)));
  assert(strcmp(url, "https://hub.example/api/v1/projects/waveshare-hodiny/ota?board=lcd7") == 0);
  // Nevejde se: prázdný výsledek, žádná useknutá adresa.
  char tiny[20];
  assert(!otaMetadataUrl(SERVER, "/waveshare-hodiny/firmware/ota.json",
                         "waveshare-hodiny", "lcd21", tiny, sizeof(tiny)));
  assert(tiny[0] == '\0');
}

void testNormalizeUrl() {
  char url[100];
  assert(otaNormalizeDownloadUrl(SERVER, "/a/b.bin", url, sizeof(url)));
  assert(strcmp(url, "https://teffteff.github.io/a/b.bin") == 0);
  assert(otaNormalizeDownloadUrl(SERVER, "https://teffteff.github.io/a/b.bin",
                                 url, sizeof(url)));
  assert(strcmp(url, "https://teffteff.github.io/a/b.bin") == 0);
  assert(!otaNormalizeDownloadUrl(SERVER, "https://teffteff.github.io.evil.com/a.bin",
                                  url, sizeof(url)));
  assert(!otaNormalizeDownloadUrl(SERVER, "https://evil.com/a.bin", url, sizeof(url)));
  assert(!otaNormalizeDownloadUrl(SERVER, "http://teffteff.github.io/a.bin", url,
                                  sizeof(url)));
  assert(!otaNormalizeDownloadUrl(SERVER, "https://teffteff.github.io", url,
                                  sizeof(url)));
  assert(!otaNormalizeDownloadUrl(SERVER, "", url, sizeof(url)));
  assert(!otaNormalizeDownloadUrl("", "/a.bin", url, sizeof(url)));
}

void testParse() {
  OtaMetadata meta;
  // Dnešní ota.json kulaté 2,1" (release 2.4.3) pole "board" nemá.
  const std::string legacy =
      "{\n  \"version\": \"2.4.3\",\n  \"chipFamily\": \"ESP32-S3\",\n"
      "  \"size\": 3253248,\n  \"sha256\": \"" + std::string(SHA) + "\",\n"
      "  \"url\": \"/waveshare-hodiny/firmware/waveshare-hodiny.ota.bin\"\n}\n";
  assert(parse(legacy, "lcd21", meta) == OtaMetadataResult::Ok);
  assert(strcmp(meta.version, "2.4.3") == 0);
  assert(strcmp(meta.board, "lcd21") == 0);
  assert(meta.size == 3253248);
  assert(strcmp(meta.sha256, SHA) == 0);
  assert(strcmp(meta.url, "https://teffteff.github.io/waveshare-hodiny/firmware/waveshare-hodiny.ota.bin") == 0);
  // Stará metadata 7" nikdy nepřijme.
  assert(parse(legacy, "lcd7", meta) == OtaMetadataResult::WrongBoard);

  // ota-lcd7.json
  const std::string lcd7 =
      metadata("lcd7", "/waveshare-hodiny/firmware/waveshare-hodiny-lcd7.ota.bin");
  assert(parse(lcd7, "lcd7", meta) == OtaMetadataResult::Ok);
  assert(strcmp(meta.board, "lcd7") == 0);
  assert(strcmp(meta.url, "https://teffteff.github.io/waveshare-hodiny/firmware/waveshare-hodiny-lcd7.ota.bin") == 0);
  assert(parse(lcd7, "lcd21", meta) == OtaMetadataResult::WrongBoard);
  // Nové ota.json s polem board.
  assert(parse(metadata("lcd21", "/a.bin"), "lcd21", meta) == OtaMetadataResult::Ok);
  assert(parse(metadata("lcd21", "/a.bin"), "lcd7", meta) == OtaMetadataResult::WrongBoard);
  // Neznámá deska.
  assert(parse(metadata("lcd43", "/a.bin"), "lcd7", meta) == OtaMetadataResult::WrongBoard);
  // "board" jiného typu než text je chyba, ne kulatá 2,1".
  {
    std::string json = metadata(nullptr, "/a.bin");
    json.insert(1, "\"board\": null,");
    assert(parse(json, "lcd21", meta) == OtaMetadataResult::Invalid);
  }

  // Obraz mimo server.
  assert(parse(metadata("lcd7", "https://evil.com/a.bin"), "lcd7", meta) ==
         OtaMetadataResult::DisallowedUrl);
  // Velká písmena v SHA-256 se převedou na malá (hash se porovnává malými).
  {
    std::string upper = SHA;
    for (char &c : upper) c = static_cast<char>(toupper(c));
    assert(parse(metadata("lcd7", "/a.bin", "1", upper.c_str()), "lcd7", meta) ==
           OtaMetadataResult::Ok);
    assert(strcmp(meta.sha256, SHA) == 0);
  }
  // Escapované lomítko v adrese (JSON ho dovoluje).
  assert(parse(metadata("lcd7", "\\/a\\/b.bin"), "lcd7", meta) == OtaMetadataResult::Ok);
  assert(strcmp(meta.url, "https://teffteff.github.io/a/b.bin") == 0);

  // Neplatná metadata.
  assert(parse(metadata("lcd7", "/a.bin", "0"), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a.bin", "-5"), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a.bin", "1.5"), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a.bin", "4294967296"), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a.bin", "\"100\""), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a.bin", "4294967295"), "lcd7", meta) == OtaMetadataResult::Ok);
  assert(parse(metadata("lcd7", "/a.bin", "1", "abc"), "lcd7", meta) == OtaMetadataResult::Invalid);
  {
    std::string json = lcd7;
    json.replace(json.find("2.5.0"), 5, "2.5");
    assert(parse(json, "lcd7", meta) == OtaMetadataResult::Invalid);
  }
  {
    std::string json = lcd7;
    json.replace(json.find("ESP32-S3"), 8, "ESP32-C3");
    assert(parse(json, "lcd7", meta) == OtaMetadataResult::Invalid);
  }
  {
    std::string json = lcd7;
    json.erase(json.find("\"url\""));
    assert(parse(json, "lcd7", meta) == OtaMetadataResult::Invalid);
  }
  // Useknutá odpověď.
  assert(parse(lcd7.substr(0, lcd7.size() / 2), "lcd7", meta) == OtaMetadataResult::Invalid);
  // Adresa delší než buffer se odmítne, ne usekne.
  assert(parse(metadata("lcd7", ("/" + std::string(300, 'a')).c_str()), "lcd7", meta) ==
         OtaMetadataResult::Invalid);
  // Neznámý escape nebo řídicí znak.
  assert(parse(metadata("lcd7", "/a\\u0041.bin"), "lcd7", meta) == OtaMetadataResult::Invalid);
  assert(parse(metadata("lcd7", "/a\t.bin"), "lcd7", meta) == OtaMetadataResult::Invalid);
  // Klíč schovaný v jiné hodnotě se nepočítá.
  {
    std::string json = metadata(nullptr, "/a.bin");
    json.insert(1, "\"note\": \"\\\"board\\\": \\\"lcd7\\\"\",");
    assert(parse(json, "lcd21", meta) == OtaMetadataResult::Ok);
  }
  assert(parse("", "lcd21", meta) == OtaMetadataResult::Invalid);
  assert(parse("[]", "lcd21", meta) == OtaMetadataResult::Invalid);
}

void feedInChunks(OtaBoardTagScanner &scanner, const std::string &data,
                  size_t chunk) {
  for (size_t at = 0; at < data.size(); at += chunk) {
    const size_t length = std::min(chunk, data.size() - at);
    scanner.feed(reinterpret_cast<const uint8_t *>(data.data() + at), length);
  }
}

bool accepts(const char *board, const std::string &image, size_t chunk = 4096) {
  OtaBoardTagScanner scanner(board);
  feedInChunks(scanner, image, chunk);
  return scanner.accepted();
}

void testBoardTag() {
  // Vlastní značka odpovídá desce překladu.
  const std::string own = std::string(OTA_BOARD_TAG_PREFIX) + BOARD_ID + ";";
  assert(strcmp(OTA_BOARD_TAG, own.c_str()) == 0);

  std::string binary(10000, '\0');
  for (size_t i = 0; i < binary.size(); ++i) binary[i] = static_cast<char>(i * 37);
  // Obraz nese vlastní značku i samotnou předponu (řetězec, kterým ji kód hledá).
  const std::string lcd7Image = binary + "HODINY_BOARD_TAG=" + std::string(1, '\0') +
                                binary + "HODINY_BOARD_TAG=lcd7;" + std::string(1, '\0') + binary;
  const std::string lcd21Image = binary + "HODINY_BOARD_TAG=lcd21;" + binary;
  for (size_t chunk : {1u, 3u, 7u, 17u, 4096u, 100000u}) {
    assert(accepts("lcd7", lcd7Image, chunk));
    assert(!accepts("lcd21", lcd7Image, chunk));
    assert(accepts("lcd21", lcd21Image, chunk));
    assert(!accepts("lcd7", lcd21Image, chunk));
  }
  // Obraz bez značky (starší release nebo cizí firmware) se nepřijme.
  assert(!accepts("lcd21", binary + binary));
  // Dvě různé značky: obraz je podezřelý.
  assert(!accepts("lcd7", lcd7Image + lcd21Image));
  // Opakovaná vlastní značka nevadí.
  assert(accepts("lcd7", lcd7Image + lcd7Image));
  // Přerušené či zdvojené začátky předpony.
  assert(accepts("lcd7", "HHODINY_BOARD_TAG=lcd7;"));
  assert(accepts("lcd7", "HODINY_BOARDHODINY_BOARD_TAG=lcd7;"));
  assert(accepts("lcd7", "HODINY_BOARD_TAG=HODINY_BOARD_TAG=lcd7;"));
  // Značka bez středníku, s velkými písmeny nebo příliš dlouhá se nepočítá.
  assert(!accepts("lcd7", "HODINY_BOARD_TAG=lcd7"));
  assert(!accepts("lcd7", "HODINY_BOARD_TAG=LCD7;"));
  assert(!accepts("lcd7", "HODINY_BOARD_TAG=;"));
  {
    OtaBoardTagScanner scanner("lcd7");
    const std::string text = "HODINY_BOARD_TAG=" + std::string(40, 'a') + ";";
    scanner.feed(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    assert(!scanner.ownTagSeen() && !scanner.otherTagSeen());
  }
  // Výchozí konstruktor hledá desku tohoto překladu.
  {
    OtaBoardTagScanner scanner;
    scanner.feed(reinterpret_cast<const uint8_t *>(OTA_BOARD_TAG), strlen(OTA_BOARD_TAG));
    assert(scanner.accepted());
  }
}

}  // namespace

int main() {
  testMetadataUrl();
  testNormalizeUrl();
  testParse();
  testBoardTag();
  printf("ota_metadata OK (%s)\n", BOARD_ID);
  return 0;
}
