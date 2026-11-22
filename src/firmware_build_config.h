#pragma once

#include <sdkconfig.h>

// Wi-Fi credentials are managed by our captive portal and serial console.
// Arduino's provisioning event callback otherwise references
// network_prov_mgr_deinit(), retaining the unused provisioning manager,
// protobuf and protocomm security implementations in the firmware.
// Load sdkconfig first so its hard-enabled default cannot restore this flag.
// This only configures source builds; the precompiled SDK is left untouched.
#undef CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
