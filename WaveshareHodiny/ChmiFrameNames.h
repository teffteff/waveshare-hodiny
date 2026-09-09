#pragma once

#include <stddef.h>
#include <time.h>

// Jména snímků radaru ČHMÚ, oddělená od stahování, aby šla testovat na
// počítači - stejně jako RainViewerIndex vedle RainViewerSource.
//
// ČHMÚ publikuje kompozici po pěti minutách pod jménem, které nese čas slotu
// v UTC:
//   pacz2gmaps3.z_max3d.20260909.1740.0.png
// Jména jsou tím pádem dopočitatelná a hodiny si je skládají z vlastního času
// místo toho, aby stahovaly výpis adresáře. Ten má přes 300 kB, protože v něm
// server drží týden snímků, a hodiny z něj potřebovaly jediný údaj: kde končí.
//
// Zrádné je na tom jediné, a proto je ta aritmetika tady a ne schovaná ve
// službě: čas ve jméně je UTC. S místním časem by jména v létě odskočila
// o dvě hodiny do budoucnosti a radar by nenašel vůbec nic.

constexpr time_t CHMI_FRAME_SLOT_SECONDS = 300;
// "pacz2gmaps3.z_max3d." + "20260909.1740" + ".0.png" a ukončovací nula.
constexpr size_t CHMI_FRAME_NAME_CAPACITY = 56;

// Začátek pětiminutového slotu, do kterého daný čas padne.
time_t chmiFrameSlotAt(time_t moment);

// Jméno snímku pro daný slot. Do output zapíše prázdný řetězec, když se čas
// nedá rozložit; volající pak nemá o co opřít dotaz.
void chmiFrameName(time_t slot, char *output, size_t capacity);

// Jména `count` snímků končící daným slotem, vzestupně od nejstaršího -
// v tomhle pořadí je čte animace. Vrací false, když se kterékoli jméno
// nepodaří složit.
bool chmiFrameNamesEndingAt(time_t newestSlot, char names[][CHMI_FRAME_NAME_CAPACITY],
                            size_t count);
