#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Dočasně přesměruje i malé alokace LVGL do PSRAM. Obrazovky, které se
// staví jednou a překreslují jen při změně dat, tak neujídají interní RAM,
// kterou potřebuje TLS - handshake si bere dva šestnáctikilobajtové buffery
// a bez souvislého místa selže s MBEDTLS_ERR_SSL_ALLOC_FAILED.
void clockLvglPreferPsram(bool prefer);

void *clockLvglAlloc(size_t size);
void *clockLvglRealloc(void *pointer, size_t size);
void clockLvglFree(void *pointer);

#ifdef __cplusplus
}
#endif
