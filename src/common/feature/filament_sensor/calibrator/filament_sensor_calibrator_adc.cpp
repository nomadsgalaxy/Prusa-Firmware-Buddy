#include "filament_sensor_calibrator_adc.hpp"

#include <logging/log.hpp>
#include <feature/filament_sensor/filament_sensor_adc_eval.hpp>
#include <raii/scope_guard.hpp>
#include <bsod/bsod.h>

LOG_COMPONENT_REF(FSensor);

FilamentSensorCalibratorADC::FilamentSensorCalibratorADC(FSensorADC &sensor)
    : FilamentSensorCalibrator(sensor) {}

bool FilamentSensorCalibratorADC::is_ready_for_calibration(CalibrationPhase phase) const {
    if (!is_fsensor_working_or_ncal_state(sensor_.get_state())) {
        return false;
    }

    if (measured_nins_range_ == ValueRange {}) {
        return true;
    }

    // If we already have some samples for the nins range, we approximate whether a filament is inserted or not just from that
    // Using the NINS range is better, because:
    // - NINS ref is measured first in the calibration procedure
    // - NINS range is much more stable than INS range. In INS, the filament can wiggle a lot, producing vastly different values

    const auto sample = sensor_.GetFilteredValue();
    const auto nins_midpoint = (measured_nins_range_.min + measured_nins_range_.max) / 2;
    const auto nins_range = (measured_nins_range_.max - measured_nins_range_.min);

    // Note: we have to use abs, because the INS range could be on either side to the NINS, depending on how a magnet in the fsensor is oriented
    const bool is_probably_inserted = std::abs(sample - nins_midpoint) > nins_range * 4;

    static_assert((int)CalibrationPhase::_cnt == 2);
    return is_probably_inserted == (phase == CalibrationPhase::inserted);
}

void FilamentSensorCalibratorADC::calibrate(CalibrationPhase phase) {
    // If the sensor is disconnected, fail the selftest
    fail_if(!is_fsensor_working_or_ncal_state(sensor_.get_state()));

    static_assert((int)CalibrationPhase::_cnt == 2);
    ValueRange *range = (phase == CalibrationPhase::inserted) ? &measured_ins_range_ : &measured_nins_range_;
    const Value sample = sensor_.GetFilteredValue();
    range->max = std::max(range->max, sample);
    range->min = std::min(range->min, sample);
}

void FilamentSensorCalibratorADC::finish() {
    log_info(FSensor,
        "fsensor ADC %i %i NINS(%" PRIi32 ", %" PRIi32 ") INS(%" PRIi32 ", %" PRIi32 ")",
        int(sensor_.id().position), int(sensor_.id().index),
        measured_nins_range_.min, measured_nins_range_.max,
        measured_ins_range_.min, measured_ins_range_.max);

    const auto store_calibration = [&](Value nins_ref, Value ins_ref) {
        const auto id = sensor_.id();
        switch (id.position) {

        case FilamentSensorID::Position::extruder:
            config_store().set_extruder_fs_ref_nins_value(id.index, nins_ref);
            config_store().set_extruder_fs_ref_ins_value(id.index, ins_ref);
            break;

        case FilamentSensorID::Position::side:
#if HAS_ADC_SIDE_FSENSOR()
            config_store().set_side_fs_ref_nins_value(id.index, nins_ref);
            config_store().set_side_fs_ref_ins_value(id.index, ins_ref);
            break;
#else
            bsod_unreachable();
#endif
        }

        static_cast<FSensorADC &>(sensor_).load_settings();
    };

    // Invalidate the reference values if we failed
    // Gets disarmed if the calibration succeeds
    ScopeGuard fail_guard = [&] {
        failed_ = true;
        store_calibration(FSensorADCEval::ref_value_not_calibrated, FSensorADCEval::ref_value_not_calibrated);
    };

    // We might have failed already, in that case it's pointless to do the math
    if (failed()) {
        return;
    }

    const std::array<Value, 4> feature_values {
        measured_nins_range_.min,
        measured_nins_range_.max,
        measured_ins_range_.min,
        measured_ins_range_.max,
    };
    for (const auto value : feature_values) {
        if (!FSensorADCEval::within_limits(value)) {
            log_error(FSensor, "Value %" PRIi32 " out of valid range", value);
            return;
        }
    }

    const Value full_range = std::ranges::max(feature_values) - std::ranges::min(feature_values);

    // Depending on the magnet orientation, nins state could be either less or more than the inserted state
    const bool nins_lt_ins = (measured_nins_range_.min < measured_ins_range_.min);

    // Choose reference INS/NINS values as the ones closest to the midpoint
    const Value nins_ref_value = nins_lt_ins ? measured_nins_range_.max : measured_nins_range_.min;
    const Value ins_ref_value = nins_lt_ins ? measured_ins_range_.min : measured_ins_range_.max;

    const Value midpoint = (nins_ref_value + ins_ref_value) / 2;

    // Verify that there is a clear separation between the samples
    // Note that the safe_zone is +- midpoint, so it basically gets multiplied by two
    // We can't be overly strict here because the INS range can be huge
    const Value safe_zone = full_range / 6;

    for (const auto value : feature_values) {
        if (std::abs(value - midpoint) <= safe_zone) {
            log_error(FSensor, "Value %" PRIi32 " is within the safe zone %" PRIi32 " +- %" PRIi32, value, midpoint, safe_zone);
            return;
        }
    }

    fail_guard.disarm();
    store_calibration(nins_ref_value, ins_ref_value);
}
