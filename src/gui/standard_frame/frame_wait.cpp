#include "frame_wait.hpp"

namespace {

const constexpr int animation_y = 130; // animation anchor point on Y axis
const constexpr int animation_x = GuiDefaults::EnableDialogBigLayout ? 220 : 110; // animation anchor point on X axis
const constexpr int text_y_offset = GuiDefaults::EnableDialogBigLayout ? 30 : 10; // text point on y axis
const constexpr int second_text_y_offset = GuiDefaults::EnableDialogBigLayout ? 67 : 45; // text point on y axis

} // namespace

FrameWait::FrameWait(window_frame_t *parent, const string_view_utf8 &text)
    : text_wait(parent, { parent->GetRect().Left(), int16_t(parent->GetRect().Top() + text_y_offset), parent->GetRect().Width(), uint16_t(30) }, is_multiline::no, is_closed_on_click_t::no, _("Please wait"))
    , text_custom(parent, { int16_t(parent->GetRect().Left() + GuiDefaults::FramePadding), int16_t(parent->GetRect().Top() + second_text_y_offset), uint16_t(parent->GetRect().Width() - 2 * GuiDefaults::FramePadding), uint16_t(60) }, is_multiline::yes, is_closed_on_click_t::no, text)
    , animation(parent, { int16_t(parent->GetRect().Left() + animation_x), int16_t(parent->GetRect().Top() + animation_y) }) //
{
    text_wait.set_font(GuiDefaults::FontBig);
    text_wait.SetAlignment(Align_t::Center());

    text_custom.set_font(GuiDefaults::FooterFont);
    text_custom.SetAlignment(Align_t::Center());
}
