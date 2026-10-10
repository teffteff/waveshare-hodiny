#include <cassert>
#include <cstring>

#include "../WaveshareHodiny/PsramGrowBuffer.h"

int main() {
  PsramGrowBuffer buffer(64, 16, 256);
  assert(buffer.data() == nullptr && buffer.capacity() == 0);
  assert(buffer.ensure());
  assert(buffer.capacity() == 64);
  assert(buffer.ensure() && buffer.capacity() == 64);

  // Obsah při růstu zůstává (trasa letadla čte buffer i po přetečení jiné).
  memset(buffer.data(), 'x', 64);
  assert(buffer.grow() && buffer.capacity() == 128);
  assert(buffer.data()[63] == 'x');
  assert(buffer.grow() && buffer.capacity() == 256);
  assert(!buffer.grow() && buffer.capacity() == 256);

  // reserve nezmenšuje a přes strop neroste.
  assert(buffer.reserve(100) && buffer.capacity() == 256);
  assert(!buffer.reserve(257) && buffer.capacity() == 256);

  buffer.release();
  assert(buffer.data() == nullptr && buffer.capacity() == 0);

  // reserve bez předchozího ensure alokuje sám a doroste na požadovanou velikost.
  assert(buffer.reserve(200) && buffer.capacity() == 200);
  buffer.release();

  // Počáteční velikost nad stropem se srazí na strop.
  PsramGrowBuffer capped(1024, 16, 96);
  assert(capped.ensure() && capped.capacity() == 96);
  assert(!capped.grow());
  capped.release();
  return 0;
}
