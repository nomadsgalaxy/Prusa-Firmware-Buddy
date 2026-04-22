/**
 * @file selftest_netstatus_interface.cpp
 */
#include "selftest_netstatus_interface.hpp"
#include "netdev.h"
#include "selftest_log.hpp"
#include <config_store/store_instance.hpp>
#include <option/has_esp.h>

LOG_COMPONENT_REF(Selftest);

static TestResultNet convert(netdev_status_t status) {
    switch (status) {
    case NETDEV_UNLINKED:
        return TestResultNet_Unlinked;
    case NETDEV_NETIF_DOWN:
        return TestResultNet_Down;
    case NETDEV_NETIF_NOADDR:
        return TestResultNet_NoAddress;
    case NETDEV_NETIF_UP:
        return TestResultNet_Up;
    default:
        break;
    }
    return TestResultNet_Unlinked; // did not use Unknown, because it means test did not run
}

static const char *to_string(netdev_status_t status) {
    switch (status) {
    case NETDEV_UNLINKED:
        return "Unlinked";
    case NETDEV_NETIF_DOWN:
        return "Down";
    case NETDEV_NETIF_NOADDR:
        return "NoAddress";
    case NETDEV_NETIF_UP:
        return "Up";
    default:
        break;
    }
    return "ERROR";
}

namespace selftest {
void phaseNetStatus() {
    SelftestResult eeres = config_store().selftest_result.get();

    netdev_status_t eth = netdev_get_status(NETDEV_ETH_ID);
    eeres.eth = convert(eth);
    log_info(Selftest, "Eth %s", to_string(eth));

#if HAS_ESP()
    netdev_status_t wifi = netdev_get_status(NETDEV_ESP_ID);
    eeres.wifi = convert(wifi);
    log_info(Selftest, "Wifi %s", to_string(wifi));
#endif

    config_store().selftest_result.set(eeres);
}

} // namespace selftest
