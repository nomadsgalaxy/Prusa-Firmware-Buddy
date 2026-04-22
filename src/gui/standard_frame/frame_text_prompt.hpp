#pragma once

#include <client_response.hpp>
#include <footer_line.hpp>
#include <radio_button_fsm.hpp>
#include <window_frame.hpp>

/**
 * Standard layout frame.
 * Contains:
 * - Centered text (alignment can be changed)
 * - A FSM radio
 */
class FrameTextPrompt {
public:
    FrameTextPrompt(window_frame_t *parent, FSMAndPhase fsm_phase, const string_view_utf8 &txt_info, Align_t info_alignment = Align_t::Center());
    /**
     * Used by WithFooter<>
     * @param footer to add to vertical stack
     */
    void add_footer(FooterLine &footer);

protected:
    window_text_t info;
    RadioButtonFSM radio;
};
