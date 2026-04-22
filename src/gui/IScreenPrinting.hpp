// IScreenPrinting.hpp
#pragma once
#include "gui.hpp"
#include "screen.hpp"
#include "window_header.hpp"
#include "status_footer.hpp"

class IScreenPrinting : public screen_t {
protected:
    window_header_t header;
    StatusFooter footer;

    virtual void stopAction() = 0;
    virtual void pauseAction() = 0;
    virtual void tuneAction() = 0;

public:
    IScreenPrinting(const string_view_utf8 &caption);
};
