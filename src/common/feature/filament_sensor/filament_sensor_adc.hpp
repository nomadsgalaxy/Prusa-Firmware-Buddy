/**
 * @file filament_sensor_adc.hpp
 * @author Radek Vana
 * @brief basic api of analog filament sensor
 * @date 2019-12-16
 */

#pragma once

#include <feature/filament_sensor/filament_sensor.hpp>
#include <utils/timing/rate_limiter.hpp>

struct metric_s;

class FSensorADC final : public IFSensor {
    friend class FilamentSensorCalibratorADC;

public:
    using Value = int32_t;

protected:
    Value fs_ref_ins_value { 0 }; ///< value of filament insert in extruder
    Value fs_ref_nins_value { 0 }; ///< value of filament not inserted in extruder

    std::atomic<Value> fs_filtered_value; ///< current filtered value set from interrupt
    static_assert(std::atomic<decltype(fs_filtered_value)::value_type>::is_always_lock_free, "Lock free type must be used from ISR.");

    /**
     * @brief Get filtered sensor-specific value.
     * @return filtered ADC value
     */
    virtual Value GetFilteredValue() const override { return fs_filtered_value.load(); };

    virtual void cycle() override;

    virtual void record_state() override;

public:
    FSensorADC(FilamentSensorID id);

    FilamentSensorCalibrator *create_calibrator(FilamentSensorCalibrator::Storage &storage) final;

    void load_settings();

    void set_filtered_value_from_IRQ(Value filtered_value);

private:
    // Limit metrics recording for each tool
    RateLimiter<uint32_t> limit_record { 49 /*ms*/ };
};
