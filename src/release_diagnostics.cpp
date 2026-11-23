#include <cstdio>
#include <cstdlib>
#include <esp_attr.h>
#include <esp_core_dump.h>
#include <esp_err.h>

#if defined(GLYPH_RELEASE_DIAGNOSTICS)
// Defining the actual assertion handler lets LTO discard unused source-level
// file/function/expression strings. Unlike NDEBUG, this preserves evaluation
// of every assertion and the fail-stop behavior when an invariant is false.
extern "C" [[noreturn]] void IRAM_ATTR __assert_func(const char *, int,
                                                   const char *, const char *) {
  abort();
}
extern "C" [[noreturn]] void IRAM_ATTR __assert(const char *, int, const char *) {
  abort();
}
// Satisfy the SDK's forced archive anchor without pulling its verbose handler
// back into the link alongside these replacements.
extern "C" void newlib_include_assert_impl(void) {}
#endif

namespace {
const char *commonErrorName(esp_err_t code) {
  switch (code) {
#define COMMON_ERROR(name) case name: return #name
    COMMON_ERROR(ESP_OK);
    COMMON_ERROR(ESP_FAIL);
    COMMON_ERROR(ESP_ERR_NO_MEM);
    COMMON_ERROR(ESP_ERR_INVALID_ARG);
    COMMON_ERROR(ESP_ERR_INVALID_STATE);
    COMMON_ERROR(ESP_ERR_INVALID_SIZE);
    COMMON_ERROR(ESP_ERR_NOT_FOUND);
    COMMON_ERROR(ESP_ERR_NOT_SUPPORTED);
    COMMON_ERROR(ESP_ERR_TIMEOUT);
    COMMON_ERROR(ESP_ERR_INVALID_RESPONSE);
    COMMON_ERROR(ESP_ERR_INVALID_CRC);
    COMMON_ERROR(ESP_ERR_INVALID_VERSION);
    COMMON_ERROR(ESP_ERR_INVALID_MAC);
    COMMON_ERROR(ESP_ERR_NOT_FINISHED);
#undef COMMON_ERROR
    default: return nullptr;
  }
}
}  // namespace

// Release builds keep serial panic reporting and reset, but do not initialize
// the flash crash-dump writer or store task snapshots after a panic.
extern "C" void __wrap_esp_core_dump_init(void) {}
extern "C" void IRAM_ATTR __wrap_esp_core_dump_write(panic_info_t *) {}

// Preserve stable string lifetimes without a heap or per-task allocation.
// SDK call sites also print the numeric error. The reentrant API below always
// provides the numeric code for errors outside the common-name subset.
extern "C" const char *__wrap_esp_err_to_name(esp_err_t code) {
  const char *name = commonErrorName(code);
  return name ? name : "ESP_ERR_UNKNOWN";
}

extern "C" const char *__wrap_esp_err_to_name_r(esp_err_t code, char *buffer,
                                               size_t size) {
  if (!buffer || !size) return buffer;
  if (const char *name = commonErrorName(code)) {
    snprintf(buffer, size, "%s", name);
  } else {
    snprintf(buffer, size, "ESP_ERR_0x%X", static_cast<unsigned int>(code));
  }
  return buffer;
}
