#include <puppies/xbuddy_extension.hpp>

#include "buddy/digest.hpp"
#include "timing.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <logging/log.hpp>
#include <mutex>
#include <puppies/PuppyBootstrap.hpp>
#include <sys/fcntl.h>
#include <sys/unistd.h>

LOG_COMPONENT_REF(MMU2);
LOG_COMPONENT_REF(Buddy);

using Lock = std::unique_lock<freertos::Mutex>;

static int open_file_id(uint16_t file_id) {
    const char *path = nullptr;
    switch (file_id) {
    case 0:
        return -1;
    case 1:
        path = "/internal/res/puppies/fw-ac_controller.bin";
        break;
    default:
        return -1;
    }

    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        log_error(Buddy, "open(%s) failed %d", path, errno);
    }
    return fd;
}

struct ClosingFileDescriptor {
    int fd;
    ~ClosingFileDescriptor() {
        if (fd != -1) {
            (void)close(fd);
        }
    }
};

template <typename T>
static buddy::puppies::CommunicationStatus write_holding(buddy::puppies::PuppyModbus &bus, uint8_t unit, T &data_struct) {
    bool dirty = true;
    const auto data = reinterpret_cast<uint16_t *>(&data_struct);
    const auto count = sizeof(data_struct) / 2;
    return bus.write_holding(unit, data, count, T::address, dirty);
}

namespace {

// The xbuddy takes action if not seen an update for 1 second (in particular,
// it signals its unhealthy status in 1-second heartbeats).
//
// Try to do at least three "activities" during that time, to have some error
// margin if something gets lost or delayed.
constexpr int32_t activity_update_every_ms = 300;

} // namespace

namespace buddy::puppies {

XBuddyExtension::XBuddyExtension(PuppyModbus &bus, uint8_t modbus_address)
    : ModbusDevice(bus, modbus_address) {}

void XBuddyExtension::set_fan_pwm(size_t fan_idx, uint8_t pwm) {
    Lock lock(mutex);

    assert(fan_idx < FAN_CNT);

    if (config.value.fan_pwm[fan_idx] != pwm) {
        config.value.fan_pwm[fan_idx] = pwm;
        config.dirty = true;
    }
}

uint8_t XBuddyExtension::get_requested_fan_pwm(size_t fan_idx) {
    Lock lock(mutex);

    assert(fan_idx < FAN_CNT);

    return config.value.fan_pwm[fan_idx];
}

uint8_t XBuddyExtension::get_flash_progress_percent() const {
    Lock lock(mutex);

    if (flash_file_size == 0) {
        return 0;
    }

    const uint32_t flash_file_offset = static_cast<uint32_t>(last_chunk_request.offset_hi << 16) | static_cast<uint32_t>(last_chunk_request.offset_lo);
    return 100 * flash_file_offset / flash_file_size;
}

bool XBuddyExtension::get_usb_power() const {
    Lock lock(mutex);
    return config.value.usb_power;
}

void XBuddyExtension::set_white_led(uint8_t intensity) {
    Lock lock(mutex);

    if (config.value.w_led_pwm != intensity) {
        config.value.w_led_pwm = intensity;
        config.dirty = true;
    }
}

void XBuddyExtension::set_white_strobe_frequency(std::optional<uint16_t> freq) {
    assert(freq != 0); // Explicit 0 makes no sense as frequency

    const uint16_t freq_raw = freq.value_or(0);

    Lock lock(mutex);

    if (config.value.w_led_frequency != freq_raw) {
        config.value.w_led_frequency = freq_raw;
        config.dirty = true;
    }
}

void XBuddyExtension::set_rgbw_led(std::array<uint8_t, 4> color) {
    Lock lock(mutex);

    bool same = true;

    // A cycle, because uint8_t vs uint16_t
    uint16_t *rgbw_fields[] = { &config.value.rgbw_led_r_pwm, &config.value.rgbw_led_g_pwm, &config.value.rgbw_led_b_pwm, &config.value.rgbw_led_w_pwm };
    for (size_t i = 0; i < 4; i++) {
        if (color[i] != *rgbw_fields[i]) {
            same = false;
        }
    }

    if (same) {
        return;
    }

    for (size_t i = 0; i < 4; i++) {
        *rgbw_fields[i] = color[i];
    }

    config.dirty = true;
}

void XBuddyExtension::set_usb_power(bool enabled) {
    Lock lock(mutex);

    if (enabled != config.value.usb_power) {
        config.value.usb_power = enabled;
        config.dirty = true;
    }
}

void XBuddyExtension::set_mmu_power(bool enabled) {
    Lock lock(mutex);

    if (enabled != config.value.mmu_power) {
        config.value.mmu_power = enabled;
        config.dirty = true;
    }
}

void XBuddyExtension::set_mmu_nreset(bool enabled) {
    Lock lock(mutex);

    if (enabled != config.value.mmu_nreset) {
        config.value.mmu_nreset = enabled;
        config.dirty = true;
    }
}

std::optional<uint16_t> XBuddyExtension::get_fan_rpm(size_t fan_idx) const {
    Lock lock(mutex);

    assert(fan_idx < FAN_CNT);

    if (!valid) {
        return std::nullopt;
    }

    return status.value.fan_rpm[fan_idx];
}

std::array<uint16_t, XBuddyExtension::FAN_CNT> XBuddyExtension::get_fans_rpm() const {
    Lock lock(mutex);

    if (!valid) {
        return std::array<uint16_t, FAN_CNT> { 0, 0, 0 };
    }

    return status.value.fan_rpm;
}

std::optional<float> XBuddyExtension::get_chamber_temp() const {
    Lock lock(mutex);

    if (!valid) {
        return std::nullopt;
    }

    return static_cast<float>(status.value.temperature) / 10.0f;
}

std::optional<XBuddyExtension::FilamentSensorState> XBuddyExtension::get_filament_sensor_state() const {
    Lock lock(mutex);

    if (!valid) {
        return std::nullopt;
    }

    return static_cast<FilamentSensorState>(status.value.filament_sensor);
}

CommunicationStatus XBuddyExtension::refresh_input(uint32_t max_age) {
    // Already locked by caller.

    const auto result = bus.read(unit, status, max_age);

    switch (result) {
    case CommunicationStatus::OK:
        valid = true;
        break;
    case CommunicationStatus::ERROR:
        valid = false;
        break;
    default:
        // SKIPPED doesn't change the validity.
        break;
    }

    return result;
}

CommunicationStatus XBuddyExtension::refresh_holding() {
    // Already locked by caller

    uint32_t now = ticks_ms();
    // We update it every time (so it's fresh), but force it written (set as
    // dirty) only once in a while. That way we can piggy-back on other
    // requests and save some round trips.

    config.value.activity = now;
    if (ticks_diff(now, last_activity_update) > activity_update_every_ms) {
        config.dirty = true;
    }

    const auto result = bus.write(unit, config);
    if (result == CommunicationStatus::OK) {
        last_activity_update = now;
    }

    return result;
}

CommunicationStatus XBuddyExtension::write_chunk() {
    if (!valid) {
        // We need up to date values of the request. Otherwise, we don't try anything.
        return CommunicationStatus::ERROR;
    }

    const xbuddy_extension::modbus::ChunkRequest &current_request = status.value.chunk_request;

    if (flash_fd != -1 && last_chunk_request.file_id != current_request.file_id) {
        // The current file there is outdated (or maybe no request any more).
        // Get rid of this one, maybe create a new one later.
        close_flash_file();
    }

    if (current_request.file_id == 0) {
        // No request -> we are done.
        return CommunicationStatus::OK;
    }

    if (flash_fd != -1) {
        if (last_chunk_request == current_request) {
            // We didn't get a newer request yet, this one was already sent, wait for newer one.
            return CommunicationStatus::SKIPPED;
        }
    } else {
        flash_fd = open_file_id(current_request.file_id);
        if (flash_fd == -1) {
            return CommunicationStatus::SKIPPED;
        }
        // Cache the file size when opening
        const off_t lseek_result = lseek(flash_fd, 0, SEEK_END);
        if (lseek_result == -1) {
            log_error(Buddy, "lseek() failed %d", errno);
            close_flash_file();
            return CommunicationStatus::SKIPPED;
        }
        flash_file_size = lseek_result;
        last_chunk_request.file_id = current_request.file_id;
    }

    const uint32_t chunk_offset = static_cast<uint32_t>(current_request.offset_hi << 16) | static_cast<uint32_t>(current_request.offset_lo);
    if (lseek(flash_fd, chunk_offset, SEEK_SET) == -1) {
        log_error(Buddy, "lseek() failed %d", errno);
        close_flash_file();
        return CommunicationStatus::ERROR;
    }

    xbuddy_extension::modbus::Chunk modbus_chunk;
    modbus_chunk.request = current_request;

    // we defined Chunk::data as little endian => no byte swapping needed
    // we also read the chunk in-place and save some stack space
    static_assert(std::endian::native == std::endian::little);
    const auto chunk_buffer = std::as_writable_bytes(std::span { modbus_chunk.data });
    const size_t chunk_size = chunk_buffer.size();

    size_t cummulative_read = 0;
    // Deal with read being able to do short reads - we promise the other side
    // we'll give it full-sized chunks (unless it's the last one).
    while (cummulative_read < chunk_size) {
        const ssize_t nread = read(flash_fd, chunk_buffer.data() + cummulative_read, chunk_size - cummulative_read);
        if (nread == 0) {
            // EOF -> terminate, send whatever we have.
            break;
        } else if (nread == -1) {
            log_error(Buddy, "read() failed %d", errno);
            close_flash_file();
            return CommunicationStatus::ERROR;
        } else {
            cummulative_read += nread;
        }
    }

    modbus_chunk.size = cummulative_read;

    CommunicationStatus transfer_status = write_holding(bus, unit, modbus_chunk);
    if (transfer_status == CommunicationStatus::OK) {
        log_debug(Buddy, "sent chunk offset %" PRIu32 " size %zu", chunk_offset, cummulative_read);
        last_chunk_request = current_request;
    }
    return transfer_status;
}

CommunicationStatus XBuddyExtension::write_digest() {
    const xbuddy_extension::modbus::DigestRequest &current_request = status.value.digest_request;

    const ClosingFileDescriptor fd { open_file_id(current_request.file_id) };
    if (fd.fd == -1) {
        return CommunicationStatus::SKIPPED;
    }

    const uint32_t salt = static_cast<uint32_t>(current_request.salt_hi << 16) | static_cast<uint32_t>(current_request.salt_lo);
    xbuddy_extension::modbus::Digest modbus_digest;
    modbus_digest.request = current_request;

    // we defined Digest::data as little endian => no byte swapping needed
    // we also compute the digest in-place and save some stack space
    static_assert(std::endian::native == std::endian::little);
    const auto buddy_digest = std::as_writable_bytes(std::span { modbus_digest.data });

    if (buddy::compute_file_digest(fd.fd, salt, buddy_digest)) {
        return write_holding(bus, unit, modbus_digest);
    } else {
        log_error(Buddy, "buddy::compute_file_digest() failed");
        return CommunicationStatus::SKIPPED;
    }
}

void XBuddyExtension::close_flash_file() {
    if (flash_fd != -1) {
        close(flash_fd);
        flash_fd = -1;
        flash_file_size = 0;
    }
}

CommunicationStatus XBuddyExtension::refresh() {
    Lock lock(mutex);

    const auto status = {
        // Refresh on every exchange in case we are flashing - we want to update
        // the request ASAP, it's changing after each sent chunk.
        refresh_input(flash_fd != -1 ? 0 : 250),
        refresh_holding(),
        refresh_log_message(),
        write_chunk(),
        write_digest(),
    };

    // Actually, failed MMU communication is not an issue on this level. Timeout and retries are being handled on the protocol_logic level
    // while MODBUS purely serves as a pass-through transport media -> no need to panic when MMU doesn't communicate now
    // Note: might change in the future, therefore keeping the same interface like refresh_input
    refresh_mmu();

    constexpr auto equal_to = [](CommunicationStatus what) {
        return [what](CommunicationStatus status) { return status == what; };
    };

    if (std::ranges::any_of(status, equal_to(CommunicationStatus::ERROR))) {
        return CommunicationStatus::ERROR;
    }
    if (std::ranges::all_of(status, equal_to(CommunicationStatus::SKIPPED))) {
        return CommunicationStatus::SKIPPED;
    }
    return CommunicationStatus::OK;
}

CommunicationStatus XBuddyExtension::initial_scan() {
    Lock lock(mutex);

    const auto input = refresh_input(0);
    config.dirty = true;
    return input;
}

CommunicationStatus XBuddyExtension::ping() {
    Lock lock(mutex);

    return refresh_input(0);
}

void XBuddyExtension::post_read_mmu_register(const uint8_t modbus_address) {
    // Post a message to the puppy task and execute the communication handshake there.
    // Fortunately, the MMU code is flexible enough - it doesn't require immediate answers
    std::lock_guard lock(mutex);
    log_debug(MMU2, "post_read_mmu_register %" PRIu8, modbus_address);
    mmuValidResponseReceived = false;
    mmuModbusRq = MMUModbusRequest::make_read_register(modbus_address);
}

void XBuddyExtension::post_write_mmu_register(const uint8_t modbus_address, const uint16_t value) {
    std::lock_guard lock(mutex);
    log_debug(MMU2, "post_write_mmu_register %" PRIu8, modbus_address);
    mmuValidResponseReceived = false;
    mmuModbusRq = MMUModbusRequest::make_write_register(modbus_address, value);
}

void XBuddyExtension::post_query_mmu() {
    std::lock_guard lock(mutex);
    log_debug(MMU2, "post_query_mmu");
    mmuValidResponseReceived = false;
    mmuModbusRq = MMUModbusRequest::make_query();
}

void XBuddyExtension::post_command_mmu(uint8_t command, uint8_t param) {
    std::lock_guard lock(mutex);
    log_debug(MMU2, "post_command_mmu");
    mmuValidResponseReceived = false;
    mmuModbusRq = MMUModbusRequest::make_command(command, param);
}

XBuddyExtension::MMUModbusRequest XBuddyExtension::MMUModbusRequest::make_read_register(uint8_t address) {
    MMUModbusRequest request;
    request.u.read.address = address;
    request.u.read.value = 0;
    request.rw = RW::read;
    return request;
}

XBuddyExtension::MMUModbusRequest XBuddyExtension::MMUModbusRequest::make_write_register(uint8_t address, uint16_t value) {
    MMUModbusRequest request;
    request.u.write.address = address;
    request.u.write.value = value;
    request.rw = RW::write;
    return request;
}

XBuddyExtension::MMUModbusRequest XBuddyExtension::MMUModbusRequest::make_query() {
    MMUModbusRequest request;
    request.u.query.pec = 0;
    request.rw = RW::query;
    return request;
}

XBuddyExtension::MMUModbusRequest XBuddyExtension::MMUModbusRequest::make_command(uint8_t command, uint8_t param) {
    MMUModbusRequest request;
    request.u.command.cp = xbuddy_extension::mmu_bridge::pack_command(command, param);
    request.rw = RW::command;
    return request;
}

CommunicationStatus XBuddyExtension::refresh_mmu() {
    // process MMU request
    // the result is written back into the MMUModbusRequest structure
    // by definition of the MMU protocol, there is only one active request-response pair on the bus

    switch (mmuModbusRq.rw) {
    case MMUModbusRequest::RW::read: {
        mmuModbusRq.rw = MMUModbusRequest::RW::read_inactive; // deactivate as it will be processed shortly
        // even if the communication fails, the MMU state machine handles it, it is not required to performs repeats at the MODBUS level
        auto rv = bus.read_holding(xbuddy_extension::mmu_bridge::modbusUnitNr, &mmuModbusRq.u.read.value, 1, mmuModbusRq.u.read.address, mmuModbusRq.timestamp_ms, 0);
        log_debug(MMU2, "read holding(uni=%" PRIu8 " val=%" PRIu16 " adr=%" PRIu16 " ts=%" PRIu32 " rv=%" PRIu8,
            xbuddy_extension::mmu_bridge::modbusUnitNr, mmuModbusRq.u.read.value, mmuModbusRq.u.read.address, mmuModbusRq.timestamp_ms, (uint8_t)rv);
        if (rv == CommunicationStatus::OK) {
            mmuModbusRq.u.read.accepted = true; // this is a bit speculative
            mmuValidResponseReceived = true;
        } else {
            mmuModbusRq.u.read.accepted = false;
        }
        return rv;
    }

    case MMUModbusRequest::RW::write: {
        mmuModbusRq.rw = MMUModbusRequest::RW::write_inactive; // deactivate as it will be processed shortly
        bool dirty = true; // force send the MODBUS message
        auto rv = bus.write_holding(xbuddy_extension::mmu_bridge::modbusUnitNr, &mmuModbusRq.u.write.value, 1, mmuModbusRq.u.write.address, dirty);
        mmuModbusRq.timestamp_ms = last_ticks_ms(); // write_holding doesn't update the timestamp, must be done manually
        log_debug(MMU2, "write holding(uni=%" PRIu8 " val=%" PRIu16 " adr=%" PRIu16 " ts=%" PRIu32 " rv=%" PRIu8,
            xbuddy_extension::mmu_bridge::modbusUnitNr, mmuModbusRq.u.write.value, mmuModbusRq.u.write.address, mmuModbusRq.timestamp_ms, (uint8_t)rv);
        if (rv == CommunicationStatus::OK) {
            mmuModbusRq.u.write.accepted = true; // this is a bit speculative
            mmuValidResponseReceived = true;
        } else {
            mmuModbusRq.u.write.accepted = false;
        }
        return rv;
    }

    case MMUModbusRequest::RW::query: {
        mmuModbusRq.rw = MMUModbusRequest::RW::query_inactive; // deactivate as it will be processed shortly
        auto rv = bus.read(xbuddy_extension::mmu_bridge::modbusUnitNr, mmuQuery, 0);
        log_debug(MMU2, "read=%" PRIu8, (uint8_t)rv);
        if (rv == CommunicationStatus::OK) {
            mmuModbusRq.timestamp_ms = mmuQuery.last_read_timestamp_ms;
            mmuValidResponseReceived = true;
        } // otherwise ignore the timestamp, MMU state machinery will time out on its own
        return rv;
    }

    case MMUModbusRequest::RW::command: {
        mmuModbusRq.rw = MMUModbusRequest::RW::command_inactive; // deactivate as it will be processed shortly
        bool dirty = true; // force send the MODBUS message
        log_debug(MMU2, "command");
        auto rv = bus.write_holding(xbuddy_extension::mmu_bridge::modbusUnitNr, &mmuModbusRq.u.command.cp, 1, xbuddy_extension::mmu_bridge::commandInProgressRegisterAddress, dirty);
        if (rv != CommunicationStatus::OK) {
            const auto [command, param] = xbuddy_extension::mmu_bridge::unpack_command(mmuModbusRq.u.command.cp);
            log_info(MMU2, "command %d failed, param %d", command, param);
            return rv;
        }

        // This is an ugly hack:
        // - First push sent command into local registers' copy - the MMU would respond with it anyway and protcol_logic expects it to be there.
        // - Then read back just the command status (register 254).
        //   Do not issue the Query directly (which would happen by querying register set 253-255)
        //   as it would send a Q0 into the MMU which would replace the command accepted/rejected in register 254.
        //
        // Beware: this command's round-trip may span over 10-20 ms which is close to the MODBUS timeout which is being used for the MMU protocol_logic as well.
        // If the round-trips become longer, MMU protocol_logic must get a larger timeout in mmu_response_received (should cause no harm afterall)
        mmuQuery.value.cip = mmuModbusRq.u.command.cp;
        rv = bus.read_holding(xbuddy_extension::mmu_bridge::modbusUnitNr, &mmuQuery.value.commandStatus, 1, xbuddy_extension::mmu_bridge::commandStatusRegisterAddress, mmuModbusRq.timestamp_ms, 0);

        if (rv == CommunicationStatus::OK) {
            log_debug(MMU2, "command query result ok");
            // timestamp_ms has been updated by bus.read_holding
            mmuValidResponseReceived = true;
        } // otherwise ignore the timestamp, MMU state machinery will time out on its own
        return rv;
    }

    default:
        return CommunicationStatus::SKIPPED;
    }
}

CommunicationStatus XBuddyExtension::refresh_log_message() {
    // Already locked by caller

    // Check if log_message_sequence changed at all
    if (status.value.log_message_sequence == last_log_message_sequence) {
        return CommunicationStatus::OK;
    }

    using xbuddy_extension::modbus::LogMessage;
    ModbusInputRegisterBlock<LogMessage::address, LogMessage> log_message;
    uint32_t max_age_ms = 0;
    const auto result = bus.read(unit, log_message, max_age_ms);
    if (result != CommunicationStatus::OK) {
        // Do not update last_log_message_sequence, it will be retried on next cycle
        log_warning(Buddy, "XBE: failed to read log message");
        return result;
    }

    // Check if we missed any messages, work in modular arithmetic
    const uint16_t expected_sequence = static_cast<uint16_t>(last_log_message_sequence + 1);
    if (log_message.value.sequence != expected_sequence) {
        // If we missed message, we at least log...
        log_info(Buddy, "XBE: missed log message(s)");
        // ...and continue with the newest message.
    }

    static_assert(std::endian::native == std::endian::little);
    const auto text_size = std::min((size_t)log_message.value.text_size, 2 * log_message.value.text_data.size());
    const auto text_data = (const char *)log_message.value.text_data.data();
    log_info(Buddy, "XBE: %.*s", text_size, text_data);

    last_log_message_sequence = log_message.value.sequence;
    return CommunicationStatus::OK;
}

bool XBuddyExtension::mmu_response_received(uint32_t rqSentTimestamp_ms) const {
    int32_t td = ~0U;
    {
        std::lock_guard lock(mutex);
        td = ticks_diff(mmuModbusRq.timestamp_ms, rqSentTimestamp_ms);
    }

    if (td >= 0) {
        log_debug(MMU2, "mmu_response_received mmr.ts=%" PRIu32 " rqst=%" PRIu32 " td=%" PRIi32, mmuModbusRq.timestamp_ms, rqSentTimestamp_ms, td);
    } // else drop negative time differences - avoid flooding the syslog
    return mmuValidResponseReceived && td >= 0 && td < PuppyModbus::MODBUS_READ_TIMEOUT_MS;
}

#ifndef UNITTESTS
XBuddyExtension xbuddy_extension(puppyModbus, PuppyBootstrap::get_modbus_address_for_dock(Dock::XBUDDY_EXTENSION));
#else
XBuddyExtension xbuddy_extension(puppyModbus, 42);
#endif

} // namespace buddy::puppies
