#pragma once

#include <Arduino.h>

// Název hodin v síti. Z něj je adresa http://<název>.local/ i jméno, pod
// kterým se hodiny hlásí routeru. Víc hodin v jedné síti se jinak přetahuje
// o waveshare-hodiny.local a to, které nastartuje později, dostane od mDNS
// náhodně přidělené waveshare-hodiny-2.
//
// Název patří konkrétním hodinám stejně jako Wi-Fi, proto neleží v ClockConfig
// a záloha ho nenese: obnova zálohy by jinak dala všem hodinám stejné jméno.

// Nejvýš 32 znaků a ukončovací nula.
constexpr size_t DEVICE_NAME_LENGTH = 33;
constexpr char DEVICE_NAME_DEFAULT[] = "waveshare-hodiny";

// Jméno v síti: malá písmena bez diakritiky, číslice a pomlčky, bez pomlčky na
// začátku a na konci (RFC 1123).
bool deviceNameValid(const char *name);

// Uložený název, nebo výchozí, když žádný uložený není nebo neplatí.
void deviceNameLoad(char *name, size_t capacity);
// Prázdný název vrátí hodiny k výchozímu.
bool deviceNamePersist(const char *name);
