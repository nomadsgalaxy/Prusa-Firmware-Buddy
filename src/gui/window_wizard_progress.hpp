/**
 * @file window_wizard_progress.hpp
 * @author Radek Vana
 * @brief Progress bar for wizard
 * @date 2020-12-07
 */

#pragma once
#include "window_progress.hpp"
#include "window_colored_rect.hpp"
#include <guiconfig/wizard_config.hpp>

class window_wizard_progress_t : public WindowProgressBar {
public:
    window_wizard_progress_t(window_t *parent, Rect16::Top_t top)
        : WindowProgressBar(parent, compute_rect(top), COLOR_BRAND, COLOR_GRAY) {}

    static Rect16 compute_rect(Rect16::Top_t top) {
        return Rect16(WizardDefaults::progress_LR_margin, top, WizardDefaults::progress_width, WizardDefaults::progress_h);
    }
};

class window_wizard_line_t : public WindowColoredRect {
public:
    window_wizard_line_t(window_t *parent, Rect16::Top_t top)
        : WindowColoredRect { parent, window_wizard_progress_t::compute_rect(top), COLOR_BRAND } {}
};
