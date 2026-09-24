#pragma once

#include <cstddef>
#include <cstdint>

// Dva celoobrazovkové snímky 480x480 (RGB565) v PSRAM, o které se dělí radar
// letadel a družice s noční oblohou. Nikdy nejsou vidět zároveň a do snímků se
// kreslí jen pro viditelnou obrazovku, takže jim stačí jeden pár místo dvou.
//
// Každá služba dřív držela vlastní pár (922 kB) napořád. S radarem ČHMÚ se
// všechno do 8 MB nevešlo: barvlevo i barvpravo 24. 9. 2026 hlásily "Pro
// družice není dostatek PSRAM" při 766 kB volných a největším bloku 410 kB,
// menším než jeden snímek. Pár se proto alokuje jednou při startu, dokud je
// PSRAM celá, a už se neuvolňuje.
enum class SharedFrameUser : uint8_t { None, Planes, Satellites };

constexpr size_t SHARED_FRAME_COUNT = 2;
constexpr size_t SHARED_FRAME_WIDTH = 480;
constexpr size_t SHARED_FRAME_HEIGHT = 480;

// Alokuje pár, pokud ještě není; true, když je k dispozici.
bool sharedFramesReserve();

// nullptr, dokud pár není alokovaný.
uint16_t *sharedFrame(size_t index);

// Předá pár obrazovce, která se právě otevírá. Volá se z hlavní smyčky při
// změně viditelnosti; nečeká, případné kreslení předchozího uživatele doběhne
// a jeho snímek se už nezveřejní.
void sharedFramesClaim(SharedFrameUser user);

// Začátek kreslení. Vrátí nenulové číslo zápůjčky, když pár patří `user`
// (nebo zatím nikomu - pak ho dostane) a nikdo jiný do něj právě nekreslí.
// Při nule se kreslit nesmí. Každé nenulové volání musí uzavřít
// sharedFramesEndRender.
uint32_t sharedFramesBeginRender(SharedFrameUser user);
void sharedFramesEndRender(SharedFrameUser user);

// Platí zápůjčka pořád? Po předání páru jinému uživateli ne: snímek nakreslený
// pod starší zápůjčkou mezitím mohl přepsat.
bool sharedFramesLeaseValid(SharedFrameUser user, uint32_t lease);
