/// \file
#pragma once

#include "IDialogMarlin.hpp"
#include "string_view_utf8.hpp"
#include "window_text.hpp"
#include "window_icon.hpp"
#include <inplace_function.hpp>

#include <gui/standard_frame/frame_wait.hpp>

class window_dlg_wait_t : public IDialogMarlin {
    FrameWait frame;

public:
    window_dlg_wait_t(Rect16 rect, const string_view_utf8 &text = {});

    window_dlg_wait_t(fsm::BaseData data);

    window_dlg_wait_t(const string_view_utf8 &text)
        : window_dlg_wait_t(GuiDefaults::DialogFrameRect, text) {}

    /// Shows the dialog and blocks the UI thread until all gcodes are finished
    /// Does this in a somewhat smart way that doesn't obstruct warnings
    static void wait_for_gcodes_to_finish();

    virtual void Change(fsm::BaseData) override;
};

/*!*********************************************************************************************************************
 * \brief GUI dialog for processes that require user to wait calmly.
 *
 * \param [in] progress_callback - function callback that returns current progress
 *
 * It creates inner gui_loop cycle that keeps GUI running while waiting.
 */
void gui_dlg_wait(stdext::inplace_function<void()> closing_callback, const string_view_utf8 &second_string = string_view_utf8::MakeNULLSTR());
