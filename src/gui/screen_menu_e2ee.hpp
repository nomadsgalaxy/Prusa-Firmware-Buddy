#pragma once

#include "screen_menu.hpp"
#include <async_job/async_job.hpp>
#include <e2ee/identity_check_levels.hpp>

namespace e2ee {
class KeyGen;
}

class ScreenMenuE2ee;

namespace detail_e2ee {

class MI_KEY final : public WI_INFO_t {
    constexpr static const char *const label = N_("Key status");

public:
    MI_KEY();
    virtual void Loop() override;
};

class MI_KEYGEN final : public IWindowMenuItem {
    constexpr static const char *const label = N_("Generate new key");

    AsyncJobWithResult<bool> key_generation;

public:
    MI_KEYGEN();

protected:
    virtual void click(IWindowMenu &window_menu) override;
};

class MI_EXPORT final : public IWindowMenuItem {
    constexpr static const char *const label = N_("Export public key");

public:
    MI_EXPORT();

protected:
    virtual void click(IWindowMenu &window_menu) override;
};

#if 0
// Disabled for now, because the identity checking is not complete
// having it disabled and the default being Accept all means, that
// the feature is invisible for users.
class MI_IDENTITY_CHECKING : public WI_SWITCH_t<3> {
    constexpr static const char *const label = N_("Identity checking");

    constexpr static const char *str_Known = N_("Known only");
    constexpr static const char *str_Ask = N_("Ask");
    constexpr static const char *str_All = N_("Accept all");

public:
    MI_IDENTITY_CHECKING();
    virtual void OnChange(size_t old_index) override;
};
#endif

// TODO:
// * Delete key? Do we need it?
using Menu = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN, MI_KEY, MI_KEYGEN, MI_EXPORT>;
} // namespace detail_e2ee

class ScreenMenuE2ee final : public detail_e2ee::Menu {
public:
    constexpr static const char *label = N_("Encryption");
    ScreenMenuE2ee();
    // Because of the unique_ptr and forward-declared class, we need a destructor elsewhere.
    ~ScreenMenuE2ee();
};
