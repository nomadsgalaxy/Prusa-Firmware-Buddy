/// @file
#pragma once

#include "window.hpp"
#include "color.hpp"
#include "Rect16.h"
#include <cstdint>

class WindowProgressBarBase : public window_t {
protected:
    Color fg_color;
    uint16_t progress_in_pixels;

    WindowProgressBarBase(window_t *parent, Rect16 rect, Color fg_color, Color bg_color);

public:
    void set_progress_percent(float val);
};

/// Progress bar widget
class WindowProgressBar : public WindowProgressBarBase {
protected:
    virtual void unconditionalDraw() override;

public:
    WindowProgressBar(window_t *parent, Rect16 rect, Color fg_color, Color bg_color)
        : WindowProgressBarBase { parent, rect, fg_color, bg_color } {}
};

/// Progress bar widget with rounded corners
class WindowRoundedProgressBar : public WindowProgressBarBase {
private:
    int corner_radius;

protected:
    virtual void unconditionalDraw() override;

public:
    WindowRoundedProgressBar(window_t *parent, Rect16 rect, Color fg_color, Color bg_color, int corner_radius)
        : WindowProgressBarBase { parent, rect, fg_color, bg_color }
        , corner_radius { corner_radius } {}
};

/**
 * @brief Draws number of circles with one current_index. All circles <= current index (or only current index if specified) have their color as "ON" ('progressed'), whilst all circles > current_index have the 'off' color (not yet done).
 * Circles always have diameter of given Rect16.Height(), so make sure the Rect is wide enough to hold all circles (there is an assert).
 */
class WindowProgressCircles : public window_t {
public:
    WindowProgressCircles(window_t *parent, Rect16 rect, uint8_t max_circles);
    [[nodiscard]] uint8_t get_current_index() const {
        return current_index;
    }
    [[nodiscard]] uint8_t get_max_circles() const {
        return max_circles;
    }
    void set_index(uint8_t new_index);
    void set_on_color(Color clr);
    void set_off_color(Color clr);
    void set_one_circle_mode(bool new_mode);
    void set_max_circles(uint8_t new_max_circles);

protected:
    void unconditionalDraw() override;

private:
    uint8_t max_circles; // how many circles should be drawn within the rect, must be >0
    uint8_t current_index { 0 }; // current progress in range [0, max_circles)
    bool one_circle_mode { false }; // true if only current_index circle should be colored as 'on', false if current_index and also all previous should be colored as 'on'
    Color color_on { COLOR_WHITE }; // color of circle that's ON
    Color color_off { COLOR_GRAY }; // color of circle that's OFF
};
