#pragma once

#include <stdint.h>
#include "IDialogMarlin.hpp"
#include "fsm_states.hpp"
#include "static_alocation_ptr.hpp"

class DialogHandler {
    template <ClientFSM fsm_, typename Dialog>
    friend struct FSMDialogDef;
    friend struct FSMDialogDefBase;
    friend struct FSMWaitDef;

    static_unique_ptr<IDialogMarlin> ptr;
    std::optional<fsm::States::Top> current_fsm_top;
    DialogHandler() = default;
    DialogHandler(DialogHandler &) = delete;

    void close(ClientFSM fsm_type);
    [[nodiscard]] bool change(ClientFSM fsm_type, fsm::BaseData data);
    [[nodiscard]] bool open(ClientFSM fsm_type, fsm::BaseData data); // can be enforced (pre opened), unlike change/close

    /// Ensures that the correct screen/dialog is open to corresponbg with fsm_states->top
    void update_screen();

public:
    // accessor for static methods
    static DialogHandler &Access();

    void Loop(); // synchronization loop, call it outside event
    bool IsOpen() const; // returns true if any dialog is active
};
