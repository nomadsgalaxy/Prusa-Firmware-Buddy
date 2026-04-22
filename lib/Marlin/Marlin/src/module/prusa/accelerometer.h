/**
 * @file
 */
#pragma once

#include "../../inc/MarlinConfigPre.h"
#include <option/has_local_accelerometer.h>
#include <option/has_remote_accelerometer.h>
#include <Marlin/src/core/types.h>
#include <utils/enum_array.hpp>

static_assert(HAS_LOCAL_ACCELEROMETER() || HAS_REMOTE_ACCELEROMETER());

#if HAS_LOCAL_ACCELEROMETER()
    #include <hwio_pindef.h>
#elif HAS_REMOTE_ACCELEROMETER()
    #include <freertos/mutex.hpp>
    #include <common/circular_buffer.hpp>
    #include <puppies/fifo_coder.hpp>
#else
    #error "Why do you #include me?"
#endif

/**
 * This class must not be instantiated globally, because (for MK3.5) it temporarily takes
 * ownership of the tachometer pin and turns it into accelerometer chip select pin.
 */
class PrusaAccelerometer {
public:
    struct Acceleration {
        float val[3];
    };

    struct RawAcceleration {
        int16_t val[3];

        Acceleration to_acceleration() const {
            return Acceleration { { raw_to_accel(val[0]), raw_to_accel(val[1]), raw_to_accel(val[2]) } };
        }
    };

    enum class Error {
        none,
        communication,
#if HAS_REMOTE_ACCELEROMETER()
        no_active_tool,
        busy,
#endif
        overflow_sensor, ///< Data not consistent, sample overrun on accelerometer sensor
#if HAS_REMOTE_ACCELEROMETER()
        overflow_buddy, ///< Data not consistent, sample missed on buddy
        overflow_dwarf, ///< Data not consistent, sample missed on dwarf
        overflow_possible, ///< Data not consistent, sample possibly lost in transfer
#endif

        _cnt
    };

    PrusaAccelerometer();
    ~PrusaAccelerometer();

    /**
     * @brief Clear buffers and Overflow
     */
    void clear();

    enum class GetSampleResult {
        ok,
        buffer_empty,
        error,
    };

    /// Convert raw sample to physical acceleration.
    constexpr static float raw_to_accel(int16_t raw) {
        constexpr float standard_gravity = 9.80665f;
        constexpr int16_t max_value = 0b0111'1111'1111'1111;
        constexpr float factor2g = 2.f * standard_gravity / max_value;
        return raw * factor2g;
    }

    /// Obtains one sample from the buffer and puts it to \param raw_acceleration (if the results is ok).
    GetSampleResult get_sample(RawAcceleration &raw_acceleration);

    GetSampleResult get_sample_printer_coords(RawAcceleration &acceleration) {
        RawAcceleration sample;
        const GetSampleResult result = get_sample(sample);
        if (result == GetSampleResult::ok) {
            acceleration = to_printer_coords(sample);
        }
        return result;
    }

    GetSampleResult get_sample_motor_coords(RawAcceleration &acceleration) {
        RawAcceleration sample;
        const GetSampleResult result = get_sample(sample);
        if (result == GetSampleResult::ok) {
            acceleration = to_motor_coords(sample);
        }
        return result;
    }

    template <typename ACCELERATION>
    static ACCELERATION to_motor_coords(ACCELERATION &sample) {
        ACCELERATION out;
#if PRINTER_IS_PRUSA_iX()
        assert(X_AXIS == A_AXIS && Y_AXIS == B_AXIS);
        // Accelerometer is fixed to the head in a way that is parallel to the logical axes and diagonal to the physical ones. Therefore, we need to perform a 45° rotation.
        static constexpr float cos45 = static_cast<float>(M_SQRT1_2);
        static constexpr float sin45 = static_cast<float>(M_SQRT1_2);
        out.val[A_AXIS] = (-sample.val[1]) * cos45 + sample.val[2] * sin45;
        out.val[B_AXIS] = (-sample.val[1]) * (-sin45) + sample.val[2] * cos45;
        out.val[Z_AXIS] = sample.val[0];
#elif PRINTER_IS_PRUSA_COREONE()
        assert(X_AXIS == A_AXIS && Y_AXIS == B_AXIS);
        // Due to accelerometer being rotated (approx. 45̀̃° = in the same way) as the head, no rotation is necessary, apart from switching axes.
        out.val[A_AXIS] = sample.val[1];
        out.val[B_AXIS] = sample.val[0];
        out.val[Z_AXIS] = sample.val[2];
#elif PRINTER_IS_PRUSA_COREONEL()
        assert(X_AXIS == A_AXIS && Y_AXIS == B_AXIS);
        out.val[A_AXIS] = -sample.val[1];
        out.val[B_AXIS] = -sample.val[0];
        out.val[Z_AXIS] = sample.val[2];
#elif PRINTER_IS_PRUSA_XL()
        static constexpr float cos45 = static_cast<float>(M_SQRT1_2);
        static constexpr float sin45 = static_cast<float>(M_SQRT1_2);
        out.val[X_AXIS] = sample.val[2] * cos45 + sample.val[1] * sin45;
        out.val[Y_AXIS] = sample.val[2] * (-sin45) + sample.val[1] * cos45;
#elif PRINTER_IS_PRUSA_MK4() || PRINTER_IS_PRUSA_MK3_5()
        // In MK printers the world and motors align
        out = to_printer_coords(sample);
#else
    #error
#endif
        return out;
    }

    template <typename ACCELERATION>
    static ACCELERATION to_printer_coords(ACCELERATION &sample) {
        ACCELERATION out;
#if PRINTER_IS_PRUSA_iX()
        // Accelerometer is fixed to the head in a way that is parallel to the logical axes. Therefore, just need to correctly swap the values.
        out.val[X_AXIS] = -sample.val[1];
        out.val[Y_AXIS] = sample.val[2];
        out.val[Z_AXIS] = sample.val[0];
#elif PRINTER_IS_PRUSA_COREONE()
        // Due to accelerometer being rotated (approx. 45̀̃° = in the same way as the motors), no rotation is necessary, apart from switching axes.
        static constexpr float cos45 = static_cast<float>(M_SQRT1_2);
        static constexpr float sin45 = static_cast<float>(M_SQRT1_2);
        out.val[X_AXIS] = sample.val[1] * cos45 - sample.val[0] * sin45;
        out.val[Y_AXIS] = sample.val[1] * sin45 + sample.val[0] * cos45;
        out.val[Z_AXIS] = sample.val[2];
#elif PRINTER_IS_PRUSA_COREONEL()
        // Accelerometer is fixed to the head in a way that is diagonal to the logical axes. Therefore, we need to perform a 45° rotation.
        static constexpr float cos45 = static_cast<float>(M_SQRT1_2);
        static constexpr float sin45 = static_cast<float>(M_SQRT1_2);
        out.val[X_AXIS] = (-sample.val[1]) * cos45 + sample.val[0] * sin45;
        out.val[Y_AXIS] = (+sample.val[1]) * sin45 + sample.val[0] * cos45;
        out.val[Z_AXIS] = sample.val[2];
#elif PRINTER_IS_PRUSA_XL()
        out.val[X_AXIS] = sample.val[2];
        out.val[Y_AXIS] = -sample.val[1];
        out.val[Z_AXIS] = -sample.val[0];
#elif PRINTER_IS_PRUSA_MK4()
        // Here we have a little conundrum. MK* attaches accelerometer to the head for X axis and then moves it to the bed for Y axis.
        // Though these values are both set here, there is no way we could read them both at the same time.
        out.val[X_AXIS] = sample.val[0];
        out.val[Y_AXIS] = sample.val[1];
        out.val[Z_AXIS] = -sample.val[2];
#elif PRINTER_IS_PRUSA_MK3_5()
        out.val[X_AXIS] = sample.val[1];
        out.val[Y_AXIS] = sample.val[1];
        // TODO find out the real angle
        static constexpr float cos45 = static_cast<float>(M_SQRT1_2);
        static constexpr float sin45 = static_cast<float>(M_SQRT1_2);
        out.val[Z_AXIS] = sample.val[1] * cos45 + sample.val[2] * sin45;
#else
    #error
#endif
        return out;
    }

    float get_sampling_rate() const;
    /**
     * @brief Get error
     *
     * Check after PrusaAccelerometer construction.
     * Check after measurement to see if it was valid.
     */
    Error get_error() const;

    /// \returns string describing the error or \p nullptr
    const char *error_str() const;

    /// If \p get_error() is not \p None, calls \p report_func(error_str)
    /// \returns \p true if there was an error and \p report_func was called
    template <typename F>
    inline bool report_error(const F &report_func) const {
        if (const auto str = error_str()) {
            report_func(str);
            return true;
        } else {
            return false;
        }
    }

#if HAS_REMOTE_ACCELEROMETER()
    static void put_sample(common::puppies::fifo::AccelerometerXyzSample sample);

    /**
     * @brief Set frequency of calling put_sample().
     * @param rate frequency [Hz]
     */
    static void set_rate(float rate);

    static void set_possible_overflow();
#endif

private:
    class ErrorImpl {
    public:
        ErrorImpl()
            : m_error(Error::none) {}
        void set(Error error) {
            if (Error::none == m_error) {
                m_error = error;
            }
        }
        Error get() const {
            return m_error;
        }
        void clear_overflow() {
            switch (m_error) {
            case Error::none:
            case Error::communication:
            case Error::_cnt:
#if HAS_REMOTE_ACCELEROMETER()
            case Error::no_active_tool:
            case Error::busy:
#endif
                break;

            case Error::overflow_sensor:
#if HAS_REMOTE_ACCELEROMETER()
            case Error::overflow_buddy:
            case Error::overflow_dwarf:
            case Error::overflow_possible:
#endif
                m_error = Error::none;
                break;
            }
        }

    private:
        Error m_error;
    };

    void set_enabled(bool enable);
#if HAS_LOCAL_ACCELEROMETER() && PRINTER_IS_PRUSA_MK3_5()
    buddy::hw::OutputEnabler output_enabler;
    buddy::hw::OutputPin output_pin;
#elif HAS_REMOTE_ACCELEROMETER()
    // Mutex is very RAM (80B) consuming for this fast operation, consider switching to critical section
    static freertos::Mutex s_buffer_mutex;
    struct SampleBuffer {
        CircularBuffer<common::puppies::fifo::AccelerometerXyzSample, 128> buffer;
        ErrorImpl error;
    };
    static SampleBuffer *s_sample_buffer;
    SampleBuffer m_sample_buffer;
    static float m_sampling_rate;
#endif
};
