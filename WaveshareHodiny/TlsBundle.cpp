#include "TlsBundle.h"

#include <esp_crt_bundle.h>
#include <ssl_client.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

// setCACertBundle() by svazek znovu nastavil; k chráněným členům, které
// zapínají jeho použití, se proto dostaneme přes ukazatele na členy.
struct BundleAccess : NetworkClientSecure {
  static constexpr auto context = &BundleAccess::sslclient;
  static constexpr auto useBundle = &BundleAccess::_use_ca_bundle;
};

}  // namespace

void tlsBundleBegin() {
  esp_crt_bundle_set(
      rootca_crt_bundle_start,
      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
}

void tlsUseCertificateBundle(NetworkClientSecure &client) {
  attach_ssl_certificate_bundle((client.*BundleAccess::context).get(), true);
  client.*BundleAccess::useBundle = true;
}
