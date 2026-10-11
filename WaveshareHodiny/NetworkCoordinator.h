#pragma once

#include <Arduino.h>

// NetworkClientSecure čeká na TLS handshake výchozích 120 s, takže jedna
// nedostupná služba by svou úlohu zablokovala na dvě minuty. Všechny služby
// proto handshake zkracují na tuhle hodnotu.
constexpr uint32_t NETWORK_TLS_HANDSHAKE_TIMEOUT_S = 5;

// Zámek níže drží jedno stažení najednou: Home Assistant a Open-Meteo
// (WaveshareHodiny.ino) i většina služeb s vlastní úlohou (zprávy, agenda,
// škola, družice, blesky, radar, letadla, výstrahy…) ho berou kolem svého
// dotazu. Nebere ho dálkové nastavení, jehož dlouhé dotazování by ho drželo
// desítky sekund. Vnitřní RAM handshakům šetří TlsMemory.cpp, který alokace
// mbedTLS posílá do PSRAM (diagnostika: tlsInternalFallbacks); svazek kořenů
// sdílený souběžnými spojeními viz TlsBundle.h.

void networkCoordinatorBegin();
bool networkCoordinatorAcquire(uint32_t timeoutMs);
void networkCoordinatorRelease();

class NetworkOperationGuard {
 public:
  explicit NetworkOperationGuard(uint32_t timeoutMs)
      : acquired_(networkCoordinatorAcquire(timeoutMs)) {}
  ~NetworkOperationGuard() {
    if (acquired_) networkCoordinatorRelease();
  }

  NetworkOperationGuard(const NetworkOperationGuard &) = delete;
  NetworkOperationGuard &operator=(const NetworkOperationGuard &) = delete;

  explicit operator bool() const { return acquired_; }

 private:
  bool acquired_;
};
