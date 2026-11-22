Import("env")

# Run after the framework has populated LINKFLAGS. A PRE script's -flto can
# otherwise be overridden by the Arduino framework's later -fno-lto.
env.ProcessUnFlags("-fno-lto")
env.AppendUnique(LINKFLAGS=["-flto"])

# The firmware uses a compact C11 formatter, retaining float, 64-bit integer,
# precision and field-width support without newlib's duplicate formatters.
env.AppendUnique(LINKFLAGS=[
    f"-Wl,--wrap={name}"
    for name in ("printf", "vprintf", "fprintf", "vfprintf", "sprintf",
                 "vsprintf", "snprintf", "vsnprintf")
])

# Release diagnostics are intentionally smaller; debug builds retain the SDK
# error-name catalogue and automatic flash core dumps.
if env.GetBuildType() == "release":
    env.AppendUnique(LINKFLAGS=[
        f"-Wl,--wrap={name}"
        for name in ("esp_core_dump_init", "esp_core_dump_write",
                     "esp_err_to_name", "esp_err_to_name_r")
    ])
