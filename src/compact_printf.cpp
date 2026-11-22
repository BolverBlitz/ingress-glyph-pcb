#include <cerrno>
#include <cstdarg>
#include <cstdio>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 1
#if defined(GLYPH_RELEASE_DIAGNOSTICS)
// Firmware and SDK diagnostics use fixed-decimal floats. Keep the broader
// scientific/general/hexadecimal float formatter available in debug builds.
#define NANOPRINTF_USE_FLOAT_HEX_FORMAT_SPECIFIER 0
#define NANOPRINTF_USE_FLOAT_SCI_FORMAT_SPECIFIER 0
#define NANOPRINTF_USE_FLOAT_SHORTEST_FORMAT_SPECIFIER 0
#else
#define NANOPRINTF_USE_FLOAT_HEX_FORMAT_SPECIFIER 1
#define NANOPRINTF_USE_FLOAT_SCI_FORMAT_SPECIFIER 1
#define NANOPRINTF_USE_FLOAT_SHORTEST_FORMAT_SPECIFIER 1
#endif
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_ALT_FORM_FLAG 1
#define NANOPRINTF_VISIBILITY_STATIC
#include "../lib/nanoprintf/nanoprintf.h"

namespace {
struct FileContext {
  FILE *file;
  bool failed;
};

void putFile(int c, void *context) {
  auto &output = *static_cast<FileContext *>(context);
  if (!output.failed && fputc(c, output.file) == EOF) output.failed = true;
}

int formatFile(FILE *file, const char *format, va_list args) {
  FileContext context{file, false};
#if defined(_MSC_VER)
  _lock_file(file);
#else
  flockfile(file);
#endif
  const int result = npf_vpprintf(putFile, &context, format, args);
#if defined(_MSC_VER)
  _unlock_file(file);
#else
  funlockfile(file);
#endif
  return context.failed || result < 0 ? -1 : result;
}
}  // namespace

extern "C" int __wrap_vsnprintf(char *buffer, size_t size,
                                  const char *format, va_list args) {
  return npf_vsnprintf(buffer, size, format, args);
}

extern "C" int __wrap_snprintf(char *buffer, size_t size,
                                 const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vsnprintf(buffer, size, format, args);
  va_end(args);
  return result;
}

extern "C" int __wrap_vsprintf(char *buffer, const char *format, va_list args) {
  // Firmware call sites provide bounded output buffers; retain libc's API.
  return npf_vsnprintf(buffer, static_cast<size_t>(-1), format, args);
}

extern "C" int __wrap_sprintf(char *buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vsprintf(buffer, format, args);
  va_end(args);
  return result;
}

extern "C" int __wrap_vfprintf(FILE *file, const char *format, va_list args) {
  return formatFile(file, format, args);
}

extern "C" int __wrap_fprintf(FILE *file, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vfprintf(file, format, args);
  va_end(args);
  return result;
}

extern "C" int __wrap_vprintf(const char *format, va_list args) {
  return __wrap_vfprintf(stdout, format, args);
}

extern "C" int __wrap_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vprintf(format, args);
  va_end(args);
  return result;
}
