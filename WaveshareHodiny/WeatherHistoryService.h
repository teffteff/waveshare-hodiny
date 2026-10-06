#pragma once

#include <stdint.h>

#include "WeatherHistory.h"

// Stahuje naměřených posledních 24 hodin (history.json) pro obrazovku
// předpovědi. Volá ji úloha předpovědi; smyčka si výsledek bere snímkem jako
// u předpovědi samotné.

void weatherHistoryServiceBegin();
// Stáhne a rozebere adresu. Vrací true, když se to povedlo; mezipaměť se
// mění jen při úspěchu, takže výpadek serveru graf nevymaže.
bool weatherHistoryServiceFetch(const char *url);
// Zahodí mezipaměť: prázdná adresa nebo vypnutá obrazovka.
void weatherHistoryServiceClear();
// Zkopíruje historii pod zámkem, když se od `generation` změnila. Vrací
// false, když se nezměnila nebo je zámek obsazený (zkusí se to příště);
// jinak `ready` říká, jestli je v `data` platná historie, nebo byla smazána.
bool weatherHistoryServiceSnapshot(uint32_t &generation,
                                   WeatherHistoryData &data, bool &ready);
