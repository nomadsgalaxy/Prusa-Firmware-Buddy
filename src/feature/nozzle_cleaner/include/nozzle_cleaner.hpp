#pragma once

#include <str_utils.hpp>

namespace nozzle_cleaner {

extern ConstexprString clean_sequence;
extern ConstexprString vblade_cut_sequence;

extern ConstexprString clean_filename;
extern ConstexprString vblade_cut_filename;

void load_clean_gcode();
void load_vblade_cut_gcode();

bool is_loader_idle();
bool is_loader_buffering();

/**
 * @brief Executes the loaded nozzle cleaner gcode.
 * The load_xxx_gcode() function must be called before this function, and gcode loaded must be in ready state for this to work correctly.
 *
 * @return true if the gcode was executed successfully
 * @return false if still buffering, failed loading or not even loaded.
 */
bool execute();

void reset();

} // namespace nozzle_cleaner
