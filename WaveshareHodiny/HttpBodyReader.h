#pragma once

#include <stddef.h>
#include <stdint.h>

// Čtení těla HTTP odpovědi s hlídaným limitem nečinnosti.
//
// HTTPClient::writeToStream() se u odpovědi s hlavičkou
// "Transfer-Encoding: chunked" nemusí nikdy vrátit: jeho smyčka končí jen tehdy,
// když spojení nahlásí uzavření, takže při zaseknutém přenosu drží TLS relaci
// navždy. Na tomto zařízení to znamenalo přes 40 kB trvale obsazené interní RAM
// a s ní nefunkční webové rozhraní i mDNS. Tento modul čte tělo sám a každý
// průchod má strop, takže stažení vždy skončí.
//
// Modul záměrně nezná Arduino síťové třídy, aby šel testovat na počítači.

// Návratové kódy jsou záporné, aby se odlišily od počtu přenesených bajtů.
enum HttpBodyStatus : int {
  HTTP_BODY_OK = 0,
  // Server přestal posílat dřív, než dorazilo celé tělo.
  HTTP_BODY_TIMEOUT = -1,
  // Rámování chunked přenosu neodpovídá RFC 9112.
  HTTP_BODY_ENCODING = -2,
  // Cíl odmítl zapsat vše, co dorazilo (typicky překročený strop velikosti).
  HTTP_BODY_WRITE = -3,
};

// Zdroj bajtů těla odpovědi. Firmware ho plní z NetworkClient, test z pole.
class HttpByteSource {
 public:
  virtual ~HttpByteSource() = default;
  // Kolik bajtů je připraveno k okamžitému přečtení.
  virtual int available() = 0;
  // Přečte nejvýše `size` bajtů. Vrací počet skutečně přečtených.
  virtual int read(uint8_t *buffer, size_t size) = 0;
  // Drží spojení ještě protistrana? Jakmile ho zavře, nemá smysl čekat na
  // další bajty až do limitu nečinnosti.
  virtual bool connected() = 0;
};

// Cíl, kam se tělo ukládá.
class HttpByteSink {
 public:
  virtual ~HttpByteSink() = default;
  // Vrátí počet přijatých bajtů. Menší hodnota než `size` znamená odmítnutí.
  virtual size_t write(const uint8_t *data, size_t size) = 0;
};

// Přenese tělo odpovědi ze `source` do `sink`.
//
// `chunked` zapíná rozbalení rámování podle hlavičky Transfer-Encoding.
// `declaredLength` je délka z hlavičky Content-Length; záporná hodnota znamená
// "dokud server posílá" a použije se jen u nerámovaného přenosu.
// `idleTimeoutMs` je nejdelší doba bez jediného nového bajtu. Uplatní se jen
// tehdy, když protistrana spojení drží otevřené; zavřené spojení končí přenos
// hned.
//
// Vrací počet bajtů zapsaných do `sink`, nebo záporný HttpBodyStatus.
int httpBodyRead(HttpByteSource &source, HttpByteSink &sink, bool chunked,
                 long declaredLength, uint32_t idleTimeoutMs);
