#include "IScreenPrinting.hpp"
#include "config.h"
#include "i18n.h"
#include "ScreenHandler.hpp"
#include "img_resources.hpp"

IScreenPrinting::IScreenPrinting(const string_view_utf8 &caption)
    : screen_t()
    , header(this)
    , footer(this) {
    IScreenPrinting::ClrMenuTimeoutClose(); // don't close on menu timeout
    header.SetText(caption);
    header.SetIcon(&img::print_16x16);
}
