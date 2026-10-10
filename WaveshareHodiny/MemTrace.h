#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef HODINY_MEMTRACE
#define HODINY_MEMTRACE 0
#endif

constexpr size_t MEMTRACE_MIN_BYTES = 8 * 1024;
constexpr size_t MEMTRACE_CAPACITY = 160;
constexpr int MEMTRACE_DEPTH = 4;

struct MemTraceEntry {
  void *pointer;
  size_t size;
  bool psram;
  uint32_t pcs[MEMTRACE_DEPTH];
};

#if HODINY_MEMTRACE
// Živé bloky od MEMTRACE_MIN_BYTES; dropped = kolik se nevešlo do tabulky.
size_t memTraceSnapshot(MemTraceEntry *output, size_t capacity, uint32_t &dropped);
#endif
