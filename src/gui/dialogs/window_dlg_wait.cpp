#include "window_dlg_wait.hpp"
#include "i18n.h"
#include "ScreenHandler.hpp"
#include <DialogHandler.hpp>
#include <client_response.hpp>
#include <marlin_vars.hpp>

static constexpr EnumArray<PhaseWait, const char *, PhaseWait::_cnt> phase_texts {
    { PhaseWait::generic, nullptr },
    { PhaseWait::homing, N_("Printer may vibrate and be noisier during homing.") },
    { PhaseWait::homing_calibration, N_("Recalibrating home. This may take some time.") },
};

window_dlg_wait_t::window_dlg_wait_t(Rect16 rect, const string_view_utf8 &text)
    : IDialogMarlin(rect)
    , frame(this, text) {
}

window_dlg_wait_t::window_dlg_wait_t(fsm::BaseData data)
    : window_dlg_wait_t(_(phase_texts[data.GetPhase()])) {}

void window_dlg_wait_t::Change(fsm::BaseData data) {
    frame.set_text(_(phase_texts[data.GetPhase()]));
}

void window_dlg_wait_t::wait_for_gcodes_to_finish() {
    // If the wait is short enough, don't show the wait dialog - it would just blink the screen
    for (int i = 0; i < 5; i++) {
        if (!marlin_vars().is_processing.get()) {
            return;
        }
        osDelay(10);
    }

    // Then show a wait dialog
    window_dlg_wait_t dlg(string_view_utf8 {});
    Screens::Access()->gui_loop_until_dialog_closed([&] {
        if (!marlin_vars().is_processing.get()) {
            Screens::Access()->Close();
            return;
        }

        // This one is important - it allows popping up a warning dialog on top of this one
        DialogHandler::Access().Loop();
    });
}

void gui_dlg_wait(stdext::inplace_function<void()> closing_callback, const string_view_utf8 &second_string) {
    window_dlg_wait_t dlg(second_string);
    Screens::Access()->gui_loop_until_dialog_closed(closing_callback);
}
