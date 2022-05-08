#pragma once

#include <stdio.h>
// clang-format off
#ifdef __clang__
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wsign-compare"
#include <mio/shared_mmap.hpp>
# pragma clang diagnostic pop
#else
#include <mio/shared_mmap.hpp>
#endif
// clang-format on

#ifdef _WIN32
#include <Rinternals.h>
#include <windows.h>
#endif

// This is needed to support wide character paths on windows
inline FILE* unicode_fopen(const char* path, const char* mode) {
  FILE* out;
#ifdef _WIN32
  // First conver the mode to the wide equivalent
  // Only usage is 2 characters so max 8 bytes + 2 byte null.
  wchar_t mode_w[10];
  MultiByteToWideChar(CP_UTF8, 0, mode, -1, mode_w, 9);

  // Then convert the path
  wchar_t* buf;
  size_t len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
  if (len <= 0) {
    Rf_error("Cannot convert file to Unicode: %s", path);
  }
  buf = (wchar_t*)R_alloc(len, sizeof(wchar_t));
  if (buf == NULL) {
    Rf_error("Could not allocate buffer of size: %ll", len);
  }

  MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, len);
  out = _wfopen(buf, mode_w);
#else
  out = fopen(path, mode);
#endif

  return out;
}

inline void print_hex(const char* string) {
  unsigned char* p = (unsigned char*) string;
  for (int i = 0; i < 300 ; i++) {
    if (p[i] == '\0') break;
    Rprintf("%c 0x%02x ", p[i], p[i]);
    if ((i%16 == 0) && i)
      Rprintf("\n");
  }
  Rprintf("\n");
}

inline mio::mmap_source
make_mmap_source(const char* file, std::error_code& error) {
#ifdef __WIN32
  // Rprintf("prepping file for make_mmap_source: %s\n", file);
  // print_hex(file);

  wchar_t* buf;
  size_t len = MultiByteToWideChar(CP_UTF8, 0, file, -1, NULL, 0);
  if (len <= 0) {
    Rf_error("Cannot convert file to Unicode: %s", file);
  }
  buf = (wchar_t*)malloc(len * sizeof(wchar_t));
  if (buf == NULL) {
    Rf_error("Could not allocate buffer of size: %ll", len);
  }

  MultiByteToWideChar(CP_UTF8, 0, file, -1, buf, len);
  mio::mmap_source out = mio::make_mmap_source(buf, error);
  free(buf);
  return out;
#else
  // Rprintf("calling make_mmap_source on file: %s\n", file);
  // print_hex(file);

  return mio::make_mmap_source(file, error);
#endif
}
