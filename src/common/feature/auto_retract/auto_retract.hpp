/// @file
#pragma once

#include <optional>
#include <bitset>
#include <inplace_function.hpp>

namespace buddy {
/// Class for managing automatic retraction after print or load, so that the printer keeps the nozzle empty for MBL and non-printing to prevent oozing.
/// Only to be managed from the marlin thread
class AutoRetract {
    friend AutoRetract &auto_retract();

public:
    using ProgressCallback = stdext::inplace_function<void(float progress_0_100)>;

    static constexpr float minimum_auto_retract_distance = 20.f; ///< Minimum retract distance for the filament to be considered auto-retracted. Auto-retracted filaments can be unloaded without heating.

    /// To hide dependency on marlin_vars
    static uint8_t current_hotend();

    /// \returns whether the specified \param hotend is retracted (some amount > 0.0f) and is a known value -> will deretract on positive Z move
    bool will_deretract(uint8_t hotend = current_hotend()) const;

    /// \returns true if the filament is completely retracted from the \param hotend's nozzle (distance >= minimum_retract_distance), allowing for cold unload.
    bool is_safely_retracted_for_unload(uint8_t hotend = current_hotend()) const;

    /// How much is the filament retracted from the nozzle (mm), std::nullopt if retracted distance not a known value
    std::optional<float> retracted_distance(uint8_t hotend = current_hotend()) const;

    /// If !is_safely_retracted_for_unload(), executes the retraction process and saves retracted distance
    void maybe_retract_from_nozzle(const ProgressCallback &progress_callback = nullptr);

    /// If will_deretract(), executes the deretraction process and set retracted distance to unknown value (because it can be changed by printing moves without notice)
    void maybe_deretract_to_nozzle();

    /// Retracts the filament quickly to minimal distance, with no ramming
    /// @param purge_length if > 0.f, extrudes this much before retracting
    void ensure_retracted_no_ramming(float purge_length = 0.f);

    /// Save values to persistent storage
    void set_retracted_distance(uint8_t hotend, std::optional<float> distance);

private:
    AutoRetract();

    /// Common checks for retract & deretract
    bool ready_to_extrude() const;

    /// Shadows the config_store variable to reduce mutex locking
    std::bitset<HOTENDS> retracted_hotends_bitset_ = 0;

    /// Keeps whether saved value in persistent storage is known or unknown (invalidated)
    std::bitset<HOTENDS> known_hotends_bitset_ = 0;

    bool is_checking_deretract_ = false;
};

AutoRetract &auto_retract();

} // namespace buddy
