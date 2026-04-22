/**
 * @file screen_menu_system.hpp
 */

#pragma once

#include "screen_menu.hpp"
#include "WindowMenuItems.hpp"
#include "MItem_tools.hpp"
#include "MItem_menus.hpp"

#include <option/has_e2ee_support.h>

using ScreenMenuSystem__ = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN, MI_SAVE_DUMP, MI_LOG_TO_TXT, MI_DEVHASH_IN_QR, MI_LOAD_SETTINGS,
#if HAS_E2EE_SUPPORT()
    MI_E2EE,
#endif
    MI_OPEN_FACTORY_RESET>;

class ScreenMenuSystem : public ScreenMenuSystem__ {
    virtual void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

public:
    constexpr static const char *label = N_("SYSTEM");
    ScreenMenuSystem();
};
