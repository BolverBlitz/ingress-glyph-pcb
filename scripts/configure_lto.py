Import("env")

# Run after the framework has populated LINKFLAGS. A PRE script's -flto can
# otherwise be overridden by the Arduino framework's later -fno-lto.
env.ProcessUnFlags("-fno-lto")
env.AppendUnique(LINKFLAGS=["-flto"])
