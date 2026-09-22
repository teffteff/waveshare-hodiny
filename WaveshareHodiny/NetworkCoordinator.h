#pragma once

#include <Arduino.h>

// NetworkClientSecure čeká na TLS handshake výchozích 120 s, takže jedna
// nedostupná služba by svou úlohu zablokovala na dvě minuty. Všechny služby
// proto handshake zkracují na tuhle hodnotu.
constexpr uint32_t NETWORK_TLS_HANDSHAKE_TIMEOUT_S = 5;

// Zámek níže už NEserializuje síť celých hodin. Berou ho jen dotazy na Home
// Assistant a Open-Meteo (fetchOpenMeteo, requestHomeAssistantState ve
// WaveshareHodiny.ino) a ty všechny běží v úloze home-assistant, takže dnes
// nikdy nečeká. Ostatní služby mají vlastní úlohy a TLS otevírají souběžně
// bez něj; vnitřní RAM jim nešetří tenhle zámek, ale TlsMemory.cpp, který
// alokace mbedTLS posílá do PSRAM (diagnostika: tlsInternalFallbacks).

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
