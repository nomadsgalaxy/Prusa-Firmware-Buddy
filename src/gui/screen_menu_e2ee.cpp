#include "screen_menu_e2ee.hpp"
#include "ScreenHandler.hpp"

#include <common/e2ee/e2ee.hpp>
#include <common/e2ee/key.hpp>
#include <common/stat_retry.hpp>

#include <sys/stat.h>

using e2ee::KeyGen;

namespace detail_e2ee {

MI_KEY::MI_KEY()
    : WI_INFO_t(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {
    Loop();
}

void MI_KEY::Loop() {
    // TODO: Shall we show some kind of fingerprint?
    // What _is_ even a fingerprint for a raw RSA key?
    ChangeInformation(_(e2ee::is_private_key_present() ? N_("Initialized") : N_("Uninitialized")));
}

MI_KEYGEN::MI_KEYGEN()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}

void MI_KEYGEN::click(IWindowMenu &) {
    const auto closing_callback = [this] {
        if (!key_generation.is_active()) {
            Screens::Access()->Close();
        }
        osDelay(1);
    };

    if (e2ee::is_private_key_present() && MsgBoxWarning(_("Are you sure you want to overwrite the encryption key? Previously encrypted G-Codes for this printer won't work."), Responses_YesNo) == Response::No) {
        return;
    }

    key_generation.issue(&e2ee::generate_key);

    const auto msgbox_builder = MsgBoxBuilder { .text = _("Generating encryption key..."), .responses = { Response::Abort }, .loop_callback = closing_callback };
    if (msgbox_builder.exec() == Response::Abort) {
        key_generation.discard();
        return;
    }

    if (!key_generation.result()) {
        MsgBoxWarning(_("Failed to generate the encryption key."), Responses_Ok);
    }
}

MI_EXPORT::MI_EXPORT()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}

void MI_EXPORT::click(IWindowMenu &) {
    if (e2ee::export_key()) {
        MsgBox(_("The public key (pubkey.der) was exported to the USB Flash disk."), Responses_Ok);
    } else {
        MsgBoxWarning(_("Failed to export the public key. Make sure USB Flash disk is inserted."), Responses_Ok);
    }
}

#if 0
MI_IDENTITY_CHECKING::MI_IDENTITY_CHECKING()
    : WI_SWITCH_t<3>(static_cast<size_t>(config_store().identity_check.get()), _(label), nullptr, is_enabled_t::yes, is_hidden_t::no,
        _(str_Known), _(str_Ask), _(str_All)) {}

void MI_IDENTITY_CHECKING::OnChange(size_t /*old_index*/) {
    config_store().identity_check.set(static_cast<e2ee::IdentityCheckLevel>(index));
}
#endif

} // namespace detail_e2ee

ScreenMenuE2ee::ScreenMenuE2ee()
    : detail_e2ee::Menu(_(label)) {
}

ScreenMenuE2ee::~ScreenMenuE2ee() {
    // Intentionally left blank;
}
