#include "frame_qr_prompt.hpp"

#include <img_resources.hpp>
#include <guiconfig/wizard_config.hpp>
#include <find_error.hpp>
#include <buddy/unreachable.hpp>
#include <auto_layout.hpp>

namespace {
static constexpr uint8_t qr_size = GuiDefaults::QRSize;
static constexpr uint8_t txt_height = WizardDefaults::txt_h;
static constexpr uint8_t spacing = 16;
static constexpr auto txt_details = N_("More details at");
static constexpr auto txt_scan_me = N_("Scan me!");

static constexpr Rect16 inner_frame_rect =
#if HAS_LARGE_DISPLAY()
    Rect16 {
        WizardDefaults::col_0,
        WizardDefaults::row_0,
        GuiDefaults::ScreenWidth - 2 * WizardDefaults::MarginLeft, // 2* = 1 left, 1 right
        qr_size + txt_height + 2 * spacing // QR code with scanMe + spacing (text is beside it)
    };
#else // MINI
    Rect16 {
        WizardDefaults::col_0,
        WizardDefaults::row_0,
        GuiDefaults::ScreenWidth - 2 * WizardDefaults::MarginLeft,
        txt_height * 6 + spacing + qr_size // 8 lines of text, spacing, QR code with scanMe  (text is above qr)
    };
#endif

static constexpr Rect16 info_text_rect =
#if HAS_LARGE_DISPLAY()
    Rect16 {
        inner_frame_rect.Left(),
        inner_frame_rect.Top(),
        GuiDefaults::ScreenWidth - qr_size - 2 * WizardDefaults::MarginLeft + spacing, // 2* = left, right, spacing =  between qr and text
        inner_frame_rect.Height()
    };
#else // MINI
    Rect16 {
        inner_frame_rect.Left(),
        inner_frame_rect.Top(),
        inner_frame_rect.Width(),
        txt_height * 6 // 5 lines of text
    };
#endif

static constexpr Rect16 qr_rect =
#if HAS_LARGE_DISPLAY()
    Rect16 {
        info_text_rect.Right() + spacing,
        inner_frame_rect.Top(),
        qr_size,
        qr_size
    };
#else // MINI
    Rect16 {
        inner_frame_rect.Left(),
        info_text_rect.Bottom() + spacing,
        qr_size,
        qr_size
    };
#endif

static constexpr Rect16 scan_me_rect =
#if HAS_LARGE_DISPLAY()
    Rect16 {
        info_text_rect.Right() + spacing,
        qr_rect.Bottom(), // no spacing here to be as close to the QR code as possible (the qr doesnt fill the whole qr_rect)
        qr_size,
        txt_height + spacing
    };
#else // MINI
    Rect16 {
        qr_rect.Right() + spacing,
        qr_rect.Top(),
        inner_frame_rect.Width() - spacing - qr_rect.Width(), // rest of space beside qr
        qr_rect.Height()
    };
#endif

static constexpr std::array layout_no_footer {
    StackLayoutItem { .height = inner_frame_rect.Height() }, // space for inner_frame
    StackLayoutItem {
        .height = StackLayoutItem::stretch,
        .margin_side = WizardDefaults::MarginLeft,
    }, // Details
    StackLayoutItem { .height = txt_height, .margin_side = WizardDefaults::MarginLeft }, // Link
    standard_stack_layout::for_radio,
};
static constexpr std::array layout_only_footer {
    standard_stack_layout::for_footer,
};
static constexpr std::array layout_with_footer = stdext::array_concat(layout_no_footer, layout_only_footer);
static_assert(layout_no_footer.size() + 1 == layout_with_footer.size(), "Layout without footer should be exactly one item (the footer) smaller than layout with footer");

} // namespace

FrameQRPrompt::FrameQRPrompt(window_frame_t *parent, FSMAndPhase fsm_phase, const string_view_utf8 &info_text, const char *qr_suffix)
    : inner_frame(parent, inner_frame_rect)
    , info(&inner_frame, info_text_rect, is_multiline::yes, is_closed_on_click_t::no, info_text)
    , scan_me(&inner_frame, scan_me_rect, is_multiline::no, is_closed_on_click_t::no, txt_scan_me)
    , qr(&inner_frame, qr_rect)
    , details(parent, {}, is_multiline::no, is_closed_on_click_t::no, txt_details)
    , link(parent, {})
    , radio(parent, {}, fsm_phase) //
{
    StringBuilder(link_buffer)
        .append_string("prusa.io/")
        .append_string(qr_suffix);
    link.SetText(string_view_utf8::MakeRAM(link_buffer.data()));

    qr.get_string_builder()
        .append_string("https://prusa.io/")
        .append_string(qr_suffix);

    qr.SetAlignment(Align_t::CenterTop());
    parent->CaptureNormalWindow(radio);
#if HAS_LARGE_DISPLAY()
    scan_me.SetAlignment(Align_t::CenterTop());
#else // MINI
    scan_me.SetAlignment(Align_t::LeftCenter());
#endif
    details.SetAlignment(Align_t::LeftBottom());
    link.SetAlignment(Align_t::LeftTop());
    link.set_font(Font::small);
    details.set_font(Font::small);

    std::array<window_t *, layout_no_footer.size()> windows_no_footer { &inner_frame, &details, &link, &radio };
    layout_vertical_stack(parent->GetRect(), windows_no_footer, layout_no_footer);
}

FrameQRPrompt::FrameQRPrompt(window_frame_t *parent, FSMAndPhase fsm_phase, std::optional<ErrCode> (*error_code_mapper)(FSMAndPhase fsm_phase))
    : FrameQRPrompt(parent, fsm_phase, string_view_utf8::MakeNULLSTR(), nullptr) {

    // Extracting information: Phase -> corresponding error code -> message
    const auto err_code = error_code_mapper(fsm_phase);
    if (!err_code.has_value()) {
        bsod_unreachable(); // Some phases do not have corresponding error codes - they should not be called with this constructor
    }

    const auto err = find_error(err_code.value());

    info.SetText(_(err.err_text));

    // link's internal buffer is used instead of link_buffer
    link.set_error_code(err_code.value());

    qr.set_error_code(err.err_code);
}

void FrameQRPrompt::add_footer(FooterLine &footer) {
    std::array<window_t *, layout_with_footer.size()> windows_with_footer { &inner_frame, &details, &link, &radio, &footer };
    layout_vertical_stack(radio.GetParent()->GetRect(), windows_with_footer, layout_with_footer);
}
