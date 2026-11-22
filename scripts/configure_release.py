Import("env")

# Configure source dependencies before Arduino/NimBLE clone the environment.
# Keep their runtime invariant checks, but omit verbose release diagnostics.
if env.GetBuildType() == "release":
    env.Append(CPPDEFINES=[
        ("GLYPH_RELEASE_DIAGNOSTICS", 1),
        ("MYNEWT_VAL_BLE_HS_LOG_LVL", 255),
    ])
