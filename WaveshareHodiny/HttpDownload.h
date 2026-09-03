#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

// Propojení HTTPClient s HttpBodyReader. Nahrazuje HTTPClient::writeToStream(),
// která u chunked odpovědi nemusí nikdy vrátit řízení a nechá viset TLS relaci.

// Uloží hlavičku Transfer-Encoding, aby se poznalo rámování odpovědi.
// HTTPClient si ji rozebere i sám, ale drží výsledek v soukromém poli bez
// veřejného čtení, takže se musí posbírat znovu. Na pořadí vůči http.begin()
// nezáleží; begin() seznam sbíraných hlaviček nemaže.
void httpDownloadPrepare(HTTPClient &http);

// Stáhne tělo odpovědi do `destination`. `idleTimeoutMs` je nejdelší doba bez
// jediného nového bajtu. Vrací počet zapsaných bajtů, nebo záporný
// HttpBodyStatus.
int httpDownloadBody(HTTPClient &http, Print &destination,
                     uint32_t idleTimeoutMs);
