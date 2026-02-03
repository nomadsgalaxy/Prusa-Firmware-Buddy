#include "nozzle_cleaner.hpp"
#include "Marlin/src/gcode/gcode.h"
#include "raii/scope_guard.hpp"
#include <gcode_loader.hpp>

namespace nozzle_cleaner {

ConstexprString clean_sequence = "G1 X254 Y285 F5000\n"
                                 "G1 X248 Y299 F5000\n"
                                 "G1 X235 Y285 F5000\n"
                                 "G1 X243 Y304 F5000\n"
                                 "G1 X230 Y291 F5000\n"
                                 "G1 X235 Y306 F5000\n"
                                 "G1 X224 Y296 F5000\n"
                                 "G1 X226 Y306 F3000\n"
                                 "G1 X248 Y288 F3000\n"
                                 "G1 X247 Y284 F3000\n"
                                 "G1 X229 Y306 F3000\n"
                                 "G1 X229 Y306 F3000\n"
                                 "G1 X254 Y285 F5000\n"
                                 "G1 X248 Y299 F5000\n"
                                 "G1 X235 Y285 F5000\n"
                                 "G1 X243 Y304 F5000\n"
                                 "G1 X230 Y291 F5000\n"
                                 "G1 X235 Y306 F5000\n"
                                 "G1 X224 Y296 F5000\n"
                                 "G1 X226 Y306 F3000\n"
                                 "G1 X248 Y288 F3000\n"
                                 "G1 X247 Y284 F3000\n"
                                 "G1 X229 Y306 F3000";

ConstexprString unload_sequence = "G1 X267.4 Y284.75 F3000\n"
                                  "G1 X253.4 Y284.75 F3000\n"
                                  "G1 X267.4 Y284.75 F3000\n"
                                  "G1 X253.4 Y284.75 F3000\n"
                                  "G27";

ConstexprString vblade_cut_sequence = "G1 X267.4 Y284.75 F3000\n"
                                      "G1 X253.4 Y284.75 F3000\n"
                                      "G1 X267.4 Y284.75 F3000\n"
                                      "G1 X253.4 Y284.75 F3000\n"
                                      "G1 X253.4 Y305.0 F3000";

ConstexprString clean_filename = "nozzle_cleaner_clean";
ConstexprString unload_filename = "nozzle_cleaner_unload";
ConstexprString vblade_cut_filename = "nozzle_cleaner_vblade_cut";

static GCodeLoader &nozzle_cleaner_gcode_loader_instance() {
    static GCodeLoader nozzle_cleaner_gcode_loader;
    return nozzle_cleaner_gcode_loader;
}

void load_clean_gcode() {
    nozzle_cleaner_gcode_loader_instance().load_gcode(clean_filename, clean_sequence);
}

void load_unload_gcode() {
    nozzle_cleaner_gcode_loader_instance().load_gcode(unload_filename, unload_sequence);
}

void load_vblade_cut_gcode() {
    nozzle_cleaner_gcode_loader_instance().load_gcode(vblade_cut_filename, vblade_cut_sequence);
}

bool is_loader_idle() {
    return nozzle_cleaner_gcode_loader_instance().is_idle();
}

bool is_loader_buffering() {
    return nozzle_cleaner_gcode_loader_instance().is_buffering();
}

bool execute() {
    // If we are idle or buffering there is no point in trying to execute but we dont want to reset if we are buffering so we just return false
    if (is_loader_idle() || is_loader_buffering()) {
        return false;
    }

    auto loader_result = nozzle_cleaner_gcode_loader_instance().get_result();
    ScopeGuard resetLoader = [&] { // Ensure the loader is always reset (the exception is if we are buffering or not idle, which is handled above)
        reset();
    };

    // this means the gcode was loaded successfully -> ready to execute it
    if (loader_result.has_value()) {
        GcodeSuite::process_subcommands_now(loader_result.value());
        return true;
    } else { // Here we have an error so we finished unsuccessfully and need to reset the loader for the next use
        return false;
    }
}

void reset() {
    nozzle_cleaner_gcode_loader_instance().reset();
}

} // namespace nozzle_cleaner
