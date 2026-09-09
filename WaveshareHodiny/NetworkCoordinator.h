#pragma once

#include <Arduino.h>

// NetworkClientSecure čeká na TLS handshake výchozích 120 s. Sdílený zámek
// sítě se přitom drží celou dobu, takže jedna nedostupná služba shodí všechny
// ostatní včetně kontroly firmware. Handshake se proto zkracuje na hodnotu,
// která se vejde do nejkratšího čekání na zámek.
constexpr uint32_t NETWORK_TLS_HANDSHAKE_TIMEOUT_S = 5;

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
