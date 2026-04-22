#include "loadcell.hpp"
#include "bsod.h"
#include "error_codes.hpp"
#include "gpio.h"
#include "metric.h"
#include "bsod.h"
#include <cmath> //isnan
#include <algorithm>
#include <numeric>
#include <limits>
#include <common/sensor_data.hpp>
#include <common/sys.hpp>
#include "timing.h"
#include <logging/log.hpp>
#include "probe_position_lookback.hpp"
#include "bsod.h"
#include "config_features.h"
#if ENABLED(POWER_PANIC)
    #include "power_panic.hpp"
#endif // POWER_PANIC
#include "../Marlin/src/module/planner.h"
#include "../Marlin/src/module/endstops.h"
#include "feature/prusa/e-stall_detector.h"
#include <metric_handlers.h>

LOG_COMPONENT_DEF(Loadcell, logging::Severity::info);

METRIC_DEF(metric_loadcell, "loadcell", METRIC_VALUE_CUSTOM, 0, METRIC_DISABLED);
METRIC_DEF(metric_loadcell_hp, "loadcell_hp", METRIC_VALUE_FLOAT, 0, METRIC_DISABLED);
METRIC_DEF(metric_loadcell_xy, "loadcell_xy", METRIC_VALUE_FLOAT, 0, METRIC_DISABLED);
METRIC_DEF(metric_loadcell_age, "loadcell_age", METRIC_VALUE_INTEGER, 0, METRIC_DISABLED);
METRIC_DEF(metric_loadcell_value, "loadcell_value", METRIC_VALUE_FLOAT, 0, METRIC_DISABLED);
METRIC_DEF(mbl, "mbl", METRIC_VALUE_CUSTOM, 0, METRIC_DISABLED);

LOG_COMPONENT_REF(Loadcell);

namespace {

struct MetricsData {
    /// Timestamp of the data
    uint32_t time_us;

    int32_t raw_value;
    int32_t offset;
    float scale;

    // Loads are in grams
    float filtered_z_load;
    float filtered_xy_load;

    float z_pos;

    constexpr inline float tared_z_load() const {
        return Loadcell::get_tared_z_load(raw_value, scale, offset);
    }
};

StaticTimer_t metrics_timer_storage;
TimerHandle_t metrics_timer;

AtomicCircularQueue<MetricsData, uint8_t, 8> metrics_queue;

// Note - the queue is quite big
// The only purpose of this assert is this comment
static_assert(sizeof(metrics_queue) == 228);

void report_loadcell_metrics(tmrTimerControl *) {
    MetricsData d;

    if (metrics_queue.isFull()) {
        log_warning(Loadcell, "Loadcell metrics overflow");
    }

    while (metrics_queue.dequeue(d)) {
        // Do the varargs passing around only if we have to
        if (metric_record_is_due(&metric_loadcell)) {
            metric_record_custom_at_time(&metric_loadcell, d.time_us, " r=%" PRId32 "i,o=%" PRId32 "i,s=%0.4f", d.raw_value, d.offset, static_cast<double>(d.scale));
        }

        metric_record_float_at_time(&metric_loadcell_hp, d.time_us, d.filtered_z_load);
        metric_record_float_at_time(&metric_loadcell_xy, d.time_us, d.filtered_xy_load);
        metric_record_integer_at_time(&metric_loadcell_age, d.time_us, ticks_diff(d.time_us, ticks_us()));
        metric_record_float_at_time(&metric_loadcell_value, d.time_us, d.tared_z_load());
        metric_record_custom(&mbl, " z=%0.3f,l=%0.3f", (double)d.z_pos, (double)d.tared_z_load());
    }
}
} // namespace

Loadcell loadcell;

Loadcell::Loadcell()
    : failsOnLoadAbove(INFINITY)
    , failsOnLoadBelow(-INFINITY)
    , highPrecision(false)
    , tareMode(TareMode::Static)
    , z_filter()
    , xy_filter() {
    Clear();

    metrics_timer = xTimerCreateStatic("loadcell_metrics", 1, false, nullptr, &report_loadcell_metrics, &metrics_timer_storage);
    assert(metrics_timer);
}

void Loadcell::WaitBarrier(uint32_t ticks_us) {
    // the first sample we're waiting for needs to be valid
    while (!planner.draining() && undefinedCnt) {
        idle(true);
    }

    // now wait until the requested timestamp
    while (!planner.draining() && ticks_diff(loadcell.GetLastSampleTimeUs(), ticks_us) < 0) {
        idle(true);
    }
}

float Loadcell::Tare(TareMode mode) {
    // ensure high-precision mode is enabled when taring
    if (!highPrecision) {
        bsod("high precision not enabled during tare");
    }

    if (tareCount != 0) {
        bsod("loadcell tare already requested");
    }

    if (endstops.is_z_probe_enabled() && (endstop || xy_endstop)) {
        fatal_error("LOADCELL", "Tare under load");
    }

    tareMode = mode;

    // request tare from ISR routine
    int requestedTareCount = tareMode == TareMode::Continuous
        ? 1 // Just to initialize the XY and Z bandpass filters
        : STATIC_TARE_SAMPLE_CNT;
    tareSum = 0;
    tareCount = requestedTareCount;

    // wait until we have all the samples that were requested
    while (!planner.draining() && tareCount != 0) {
        idle(true);
    }

    // We might have exited the loop prematurely because of planner.draining()
    // In that case, reset tare count to 0.
    // This is safe - the tareCount is consumed from the ISR, which has a higher priority
    tareCount = 0;

    if (!planner.draining()) {
        if (tareMode == TareMode::Continuous) {
            // double-check filters are ready after the tare
            assert(z_filter.initialized());
            assert(xy_filter.initialized());
        }

        offset = tareSum / requestedTareCount;

        // Wait till the loadcell reports first sample with the new offset applied (BFW-7791)
        WaitBarrier();
    }

    reset_endstops();

    return offset * scale; // Return offset scaled to output grams
}

void Loadcell::Clear() {
    tareCount = 0;
    loadcellRaw = undefined_value;
    undefinedCnt = -UNDEFINED_INIT_MAX_CNT;
    offset = 0;
    reset_filters();
    reset_endstops();
}

void Loadcell::reset_filters() {
    z_filter.reset();
    xy_filter.reset();
    loadcell.analysis.Reset();
}

void Loadcell::reset_endstops() {
    const bool changed = endstop || xy_endstop;

    endstop = false;
    xy_endstop = false;

    if (changed) {
        // Warning: Calling endstops from outside of ISR - needs to be reworked
        // BFW-7674
        buddy::hw::zMin.isr();
    }
}

bool Loadcell::GetMinZEndstop() const {
    return endstop;
}

bool Loadcell::GetXYEndstop() const {
    return xy_endstop;
}

float Loadcell::GetScale() const {
    return scale;
}

float Loadcell::GetHysteresis() const {
    return hysteresis;
}

int32_t Loadcell::get_raw_value() const {
    return loadcellRaw;
}

void Loadcell::SetFailsOnLoadAbove(float failsOnLoadAbove) {
    this->failsOnLoadAbove = failsOnLoadAbove;
}

float Loadcell::GetFailsOnLoadAbove() const {
    return failsOnLoadAbove;
}

void Loadcell::set_xy_endstop(const bool enabled) {
    xy_endstop_enabled = enabled;
}

void Loadcell::ProcessSample(int32_t loadcellRaw, uint32_t time_us) {
    if (loadcellRaw != undefined_value) {
        this->loadcellRaw = loadcellRaw;
        this->undefinedCnt = 0;
    } else {
        if (!HAS_LOADCELL_HX717() || (!sys_debugger_attached() || (TERN0(DEBUG_LEVELING_FEATURE, DEBUGGING(LEVELING)) || DEBUGGING(ERRORS)))) {
            // see comment in hx717mux: only enable additional safety checks if HX717 is multiplexed
            // and directly attached without an active debugging session or LEVELING/ERROR flags, to
            // avoid triggering inside other breakpoints.

            // undefined value, use forward-fill only for short bursts
            if (++this->undefinedCnt > UNDEFINED_SAMPLE_MAX_CNT) {
                fatal_error(ErrCode::ERR_SYSTEM_LOADCELL_TIMEOUT);
            }
        }
    }

    const float tared_z_load = get_tared_z_load(loadcellRaw, scale, offset);

    float filtered_z_load = NAN;
    float filtered_xy_load = NAN;

    // handle filters only in high precision mode
    if (highPrecision) {
        filtered_z_load = z_filter.filter(this->loadcellRaw) * scale;
        filtered_xy_load = xy_filter.filter(this->loadcellRaw) * scale;

        if (tareCount != 0) {
            // Undergoing tare process, only use valid samples
            if (loadcellRaw != undefined_value) {
                tareSum += loadcellRaw;
                tareCount -= 1;
            }
        } else {
            // Trigger Z endstop/probe
            float loadForEndstops, threshold;
            if (tareMode == TareMode::Static) {
                loadForEndstops = tared_z_load;
                threshold = thresholdStatic;
            } else {
                assert(!Endstops::is_z_probe_enabled() || z_filter.initialized());
                loadForEndstops = filtered_z_load;
                threshold = thresholdContinuous;
            }

            if (endstop && loadForEndstops >= (threshold + hysteresis)) {
                endstop = false;
                buddy::hw::zMin.isr();

            } else if (!endstop && loadForEndstops <= threshold && endstops.is_z_probe_enabled()) {
                endstop = true;
                buddy::hw::zMin.isr();
            }

            // Trigger XY endstop/probe
            if (xy_endstop_enabled) {
                assert(xy_filter.initialized());

                // Everything as absolute values, watch for changes.
                // Load perpendicular to the sensor sense vector is not guaranteed to have defined sign.
                if (abs(filtered_xy_load) > abs(XY_PROBE_THRESHOLD)) {
                    xy_endstop = true;
                    buddy::hw::zMin.isr();
                }
                if (abs(filtered_xy_load) < abs(XY_PROBE_THRESHOLD) - abs(XY_PROBE_HYSTERESIS)) {
                    xy_endstop = false;
                    buddy::hw::zMin.isr();
                }
            }
        }
    }

    // save sample timestamp/age
    last_sample_time_us = time_us;

    const float z_pos = buddy::probePositionLookback.get_position_at(time_us);

    const MetricsData metrics_data {
        .time_us = time_us,
        .raw_value = loadcellRaw,
        .offset = offset,
        .scale = scale,
        .filtered_z_load = filtered_z_load,
        .filtered_xy_load = filtered_xy_load,
        .z_pos = z_pos,
    };
    if (are_metrics_enabled()) {
        const bool needs_timer_start = metrics_queue.isEmpty();
        if (!metrics_queue.enqueue(metrics_data)) {
            metrics_queue.clear();
        }
        if (needs_timer_start && !xTimerStartFromISR(metrics_timer, nullptr)) {
            // If we failed to start the timer, clear the queue so that we can try starting it again next time
            metrics_queue.clear();
        }
    }

    sensor_data().loadCell = tared_z_load;
    if (!std::isfinite(tared_z_load)) {
        fatal_error(ErrCode::ERR_SYSTEM_LOADCELL_INFINITE_LOAD);
    }

    if (Endstops::is_z_probe_enabled()) {
#if 0
      // TODO: temporarily disabled for release until true overloads are resolved
      // load is negative, so flip the signs accordingly just below
        if ((isfinite(failsOnLoadAbove) && loadForEndstops < -failsOnLoadAbove)
            || (isfinite(failsOnLoadBelow) && loadForEndstops > -failsOnLoadBelow))
            fatal_error("LOADCELL", "Loadcell overload");
#endif
    }

    // push sample for analysis
    // If the sample is nan, the analysis should detect it and fail
    analysis.StoreSample(time_us, z_pos, tared_z_load);

    if (std::isnan(z_pos)) {
        // Temporary disabled as this causes positive feedback loop by blocking the calling thread if the logs are
        // being uploaded to a remote server. This does not solve the problem entirely. There are other logs that
        // can block. Still this should fix most of the issues and allow us to test the rest of the functionality
        // until a proper solution is found.
        // log_warning(Loadcell, "Got NaN z-coordinate; skipping (age=%dus)", ticks_us_from_now);
    }

    // Perform E motor stall detection
    EMotorStallDetector::Instance().ProcessSample(this->loadcellRaw);
}

void Loadcell::HomingSafetyCheck() const {
    // We need signed int because the last sample can be slightly in the future, caused by time sync with dwarves.
    static constexpr int32_t MAX_LOADCELL_DATA_AGE_WHEN_HOMING_US = 100000;
    int32_t since_last = ticks_diff(ticks_us(), last_sample_time_us.load());
    if (since_last > MAX_LOADCELL_DATA_AGE_WHEN_HOMING_US) {
        fatal_error(ErrCode::ERR_ELECTRO_HOMING_ERROR_Z);
    }
}

/**
 * @brief Create object enforcing error when positive load value is too big
 *
 * Sets grams threshold when created, restores to original when destroyed.
 * @param grams
 * @param enable Enable condition. Useful if you want to create enforcer based on condition.
 *              You can not put object simply inside if block, because you unintentionally also
 *              limit its scope.
 *   @arg @c true Normal operation
 *   @arg @c false Do not set grams threshold when created. (Doesn't affect destruction.)
 */

Loadcell::FailureOnLoadAboveEnforcer Loadcell::CreateLoadAboveErrEnforcer(bool enable, float grams) {
    return Loadcell::FailureOnLoadAboveEnforcer(*this, enable, grams);
}

/**
 *
 * @param lcell
 * @param grams
 * @param enable
 */
Loadcell::FailureOnLoadAboveEnforcer::FailureOnLoadAboveEnforcer(Loadcell &lcell, bool enable, float grams)
    : lcell(lcell)
    , oldErrThreshold(lcell.GetFailsOnLoadAbove()) {
    if (enable) {
        lcell.SetFailsOnLoadAbove(grams);
    }
}
Loadcell::FailureOnLoadAboveEnforcer::~FailureOnLoadAboveEnforcer() {
    lcell.SetFailsOnLoadAbove(oldErrThreshold);
}

/**
 * @brief Create object enabling high precision mode
 *
 * Keep high precision enabled when created, then restore when destroyed
 * @param enable Enable condition. Useful if you want to create enforcer based on condition.
 *              You can not put object simply inside if block, because you unintentionally also
 *              limit its scope.
 *   @arg @c true Normal operation
 *   @arg @c false Do not enable high precision mode and tare when created.
 */
Loadcell::HighPrecisionEnabler::HighPrecisionEnabler(Loadcell &lcell,
    bool enable)
    : m_lcell(lcell)
    , m_enable(enable) {
    if (m_enable) {
        m_lcell.EnableHighPrecision();
    }
}

Loadcell::HighPrecisionEnabler::~HighPrecisionEnabler() {
    if (m_enable) {
        m_lcell.DisableHighPrecision();
    }
}
