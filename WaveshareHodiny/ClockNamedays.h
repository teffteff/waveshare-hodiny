#pragma once

// Český kalendář jmen (jmeniny). Generováno z OzzyCzech/namedays-cs (lib/names.json).
// Jména jsou uložená VELKÝMI písmeny, protože font clock_czech obsahuje jen
// velké české znaky s diakritikou; drobná písmena s háčky a čárkami v něm nejsou.
// Vrací ukazatel do flash, nebo nullptr pro dny bez jmenin (např. 1. ledna).
// month 1-12, day 1-31.
const char *czechNamedayFor(int month, int day);
