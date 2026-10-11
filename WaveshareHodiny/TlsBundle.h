#pragma once

#include <WiFiClientSecure.h>

// Svazek kořenů Mozilly pro služby, jejichž adresu zadává uživatel.
//
// NetworkClientSecure::setCACertBundle() při každém volání zavolá
// esp_crt_bundle_set(), které uvolní a znovu alokuje JEDINÉ globální pole
// certifikátů, a esp_crt_bundle_detach() ho uvolní úplně. Služby běží
// v samostatných úlohách a TLS otevírají souběžně, takže jedna úloha mohla
// pole uvolnit, zatímco z něj ověřovací callback jiné úlohy právě četl.
// Opakované nastavení přitom nic neztrácí - předchozí pole se uvolní.
//
// Proto se svazek nastaví jednou při startu a už se neodpojuje (pole ukazatelů
// zabere necelý kilobajt); klient si jen zapne ověřování proti němu.
void tlsBundleBegin();
void tlsUseCertificateBundle(NetworkClientSecure &client);
