#pragma once

#include <cstddef>
#include <cstdint>

// Buffer odpovědi v PSRAM, který začíná malý a roste, až když se do něj
// odpověď nevejde.
//
// Služby si dřív braly celý strop naráz (radar letadel 512 kB, zprávy 160 kB,
// družice a blesky 96 kB), a to až při prvním použití. Za pár hodin běhu ale
// radar ČHMÚ a ostatní PSRAM rozdrobí: barvpravo 10. 10. 2026 hlásilo "Pro
// radar letadel není dostatek PSRAM" při 764 kB volných a největším bloku
// 434 kB, barvlevo mělo největší blok 79 kB. Skutečné odpovědi mají přitom
// jednotky až desítky kB. Zvětšený buffer zůstává, takže se roste nanejvýš
// párkrát za běh.
//
// Není vláknově bezpečný; používá ho vždy jen úloha, která stahuje.
class PsramGrowBuffer {
 public:
  // initial: první pokus o alokaci; při neúspěchu se půlí až k minimum.
  // maximum: strop, přes který buffer neroste.
  PsramGrowBuffer(size_t initial, size_t minimum, size_t maximum)
      : initial_(initial), minimum_(minimum), maximum_(maximum) {}
  PsramGrowBuffer(const PsramGrowBuffer &) = delete;
  PsramGrowBuffer &operator=(const PsramGrowBuffer &) = delete;

  // Alokuje, pokud ještě nic nemá; true, když buffer je.
  bool ensure();
  // Zvětší buffer aspoň na `size` bajtů (nejvýš na strop). Při neúspěchu
  // zůstává původní buffer i s obsahem.
  bool reserve(size_t size);
  // Zdvojnásobí buffer, nejvýš na strop. false na stropu nebo bez paměti.
  bool grow();
  void release();

  uint8_t *data() const { return data_; }
  size_t capacity() const { return capacity_; }
  size_t maximum() const { return maximum_; }

 private:
  size_t initial_;
  size_t minimum_;
  size_t maximum_;
  uint8_t *data_ = nullptr;
  size_t capacity_ = 0;
};
