#pragma once
#include "gui.hpp"
#include "screen.hpp"
#include <guiconfig/guiconfig.h>
#include <buddy/bootstrap_state.hpp>
#include <utils/progress_mapper.hpp>

class ScreenSplash : public screen_t {
    window_text_t text_progress;
    WindowRoundedProgressBar progress;

    char text_progress_buffer[32];

public:
    ScreenSplash();
    ~ScreenSplash();

private:
    virtual void draw() override;

    ProgressMapper<buddy::BootstrapStage> progress_mapper;

protected:
    virtual void windowEvent(window_t *sender, GUI_event_t event, void *param) override;
};
