#include "frame_text_prompt.hpp"
#include "auto_layout.hpp"

namespace {
static constexpr std::array layout_no_footer {
    StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 16, .margin_top = 16 },
    standard_stack_layout::for_radio,
};
static constexpr std::array layout_only_footer {
    standard_stack_layout::for_footer,
};
static constexpr std::array layout_with_footer = stdext::array_concat(layout_no_footer, layout_only_footer);
static_assert(layout_no_footer.size() + 1 == layout_with_footer.size(), "Layout without footer should be exactly one item (the footer) smaller than layout with footer");
} // namespace

FrameTextPrompt::FrameTextPrompt(window_frame_t *parent, FSMAndPhase fsm_phase, const string_view_utf8 &txt_info, Align_t info_alignment)
    : info(parent, {}, is_multiline::yes, is_closed_on_click_t::no, txt_info)
    , radio(parent, {}, fsm_phase) {

    info.SetAlignment(info_alignment);
#if HAS_MINI_DISPLAY()
    info.set_font(Font::small);
#endif

    parent->CaptureNormalWindow(radio);

    std::array<window_t *, layout_no_footer.size()> windows_no_footer { &info, &radio };
    layout_vertical_stack(parent->GetRect(), windows_no_footer, layout_no_footer);
}

void FrameTextPrompt::add_footer(FooterLine &footer) {
    std::array<window_t *, layout_with_footer.size()> windows_with_footer { &info, &radio, &footer };
    layout_vertical_stack(info.GetParent()->GetRect(), windows_with_footer, layout_with_footer);
}
