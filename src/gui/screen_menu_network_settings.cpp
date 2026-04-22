#include "screen_menu_network_settings.hpp"
#include "wui_api.h"
#include <raii/auto_restore.hpp>
#include "ScreenHandler.hpp"
#include "netdev.h"
#include <http_lifetime.h>
#include "marlin_client.hpp"

// ------------------- ETHERNET -----------------------
ScreenMenuEthernetSettings::ScreenMenuEthernetSettings()
    : ScreenMenuEthernetSettings_(_(label)) {
}

#if HAS_ESP()
// ------------------------ WIFI -----------------------------------
ScreenMenuWifiSettings::ScreenMenuWifiSettings()
    : ScreenMenuWifiSettings_(_(label)) {
}
#endif
