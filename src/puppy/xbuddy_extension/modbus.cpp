/// @file
#include "modbus.hpp"

#include <xbuddy_extension/mmu_bridge.hpp>

#include <cstdlib>
#include <cassert>

using Status = modbus::Callbacks::Status;

static constexpr const std::byte read_holding_registers { 0x03 };
static constexpr const std::byte read_input_registers { 0x04 };
static constexpr const std::byte write_multiple_registers { 0x10 };

static constexpr std::byte modbus_byte_lo(uint16_t value) {
    return std::byte(value & 0xff);
}

static constexpr std::byte modbus_byte_hi(uint16_t value) {
    return std::byte(value >> 8);
}

namespace modbus {

Dispatch::Dispatch(std::span<Device> devices)
    : devices { devices } {
    if (!all_distinct()) {
        abort();
    }
}

modbus::Callbacks *Dispatch::get_device(uint8_t id) {
    for (const auto &device : devices) {
        if (device.id == id) {
            return &device.callbacks;
        }
    }

    return nullptr;
}

bool Dispatch::all_distinct() {
    for (size_t i = 0; i < devices.size(); i++) {
        for (size_t j = i + 1; j < devices.size(); j++) {
            if (devices[i].id == devices[j].id) {
                return false;
            }
        }
    }

    return true;
}

uint16_t compute_crc(std::span<const std::byte> bytes) {
    uint16_t crc = 0xffff;
    for (auto byte : bytes) {
        crc ^= uint16_t(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc >>= 1;
                crc ^= 0xa001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// temporary duplicate definition from app.cpp, do properly
constexpr uint16_t MY_MODBUS_ADDR = 0x1a + 7;
constexpr uint16_t MMU_MODBUS_ADDR = xbuddy_extension::mmu_bridge::modbusUnitNr;

std::span<std::byte> handle_transaction(
    Dispatch &dispatch,
    std::span<std::byte> request,
    std::span<std::byte> response_buffer) {

    if (request.size() < 4 || modbus::compute_crc(request) != 0) {
        return {};
    }

    assert(reinterpret_cast<intptr_t>(response_buffer.data()) % alignof(uint16_t) == 0);
    assert(reinterpret_cast<intptr_t>(request.data()) % alignof(uint16_t) == 0);
    std::byte *orig_request = request.data();

    auto response = response_buffer.begin();
    const auto resp_end = response_buffer.end();
    auto resp = [&](std::byte b) {
        if (response < resp_end) {
            *response++ = b;
        } else {
            // Short output buffer. Do we have a better strategy? As this is a
            // result of a mismatch between our buffer (the extboard FW) and
            // the printer, programmer needs to fix it, it's not some kind of
            // external condition...
            abort();
        }
    };

    const auto device_id = request[0];
    modbus::Callbacks *device_callbacks = dispatch.get_device(static_cast<uint8_t>(device_id));
    if (!device_callbacks) {
        return {};
    }

    const auto function = request[1];
    request = request.subspan(2, request.size() - 4);

    resp(device_id);
    resp(function);

    auto status = Status::Ok;

    auto get_word = [&](size_t offset) {
        uint16_t high = static_cast<uint8_t>(request[offset]);
        uint16_t low = static_cast<uint8_t>(request[offset + 1]);
        return high << 8 | low;
    };

    switch (function) {
    case read_holding_registers:
    case read_input_registers: {
        if (request.size() != 4) {
            return {};
        } else {
            const uint16_t address = get_word(0);
            const uint16_t count = get_word(2);

            const uint8_t bytes = 2 * count;
            resp(std::byte { bytes });

            // Using the response buffer for the data directly, then arranging the endians in-place.
            // This _should_ be OK, as uint8_t/byte is allowed to alias, so we
            // are fine pointing both byte * and uint16_t * into the same
            // place.
            std::byte *out_buffer = response_buffer.data() + (response - response_buffer.begin());
            if (reinterpret_cast<intptr_t>(out_buffer) % alignof(uint16_t) != 0) {
                // Should be OK, there's one extra byte for CRC
                out_buffer++;
            }
            if ((resp_end - response - 1 /*for the ++/CRC above*/) / 2 < count) {
                abort();
            }
            std::span<uint16_t> out(reinterpret_cast<uint16_t *>(out_buffer), count);
            status = device_callbacks->read_registers(address, out);

            if (status != Status::Ok) {
                break;
            }
            for (size_t i = 0; i < count; i++) {
                uint16_t value = out[i];
                resp(modbus_byte_hi(value));
                resp(modbus_byte_lo(value));
            }
        }
    } break;
    case write_multiple_registers: {
        if (request.size() < 5) {
            return {};
        } else {
            const uint16_t address = get_word(0);
            const uint16_t count = get_word(2);
            const uint8_t bytes = (uint8_t)request[4];
            request = request.subspan(5);
            if (request.size() < bytes || bytes < count * 2) {
                // Incomplete message.
                return {};
            }

            std::span<uint16_t> in(reinterpret_cast<uint16_t *>(orig_request), count);
            // Rearanging bytes in-place in the buffer. Allowed, since:
            // * The input is aligned (we require it in our interface and we have checked).
            // * byte and uint16_t are allowed to alias.
            // * We overwrite only the ones we have already read.
            // * Function return is a sequence point.
            for (size_t i = 0; i < count; ++i) {
                in[i] = get_word(0);
                request = request.subspan(2);
            }
            status = device_callbacks->write_registers(address, in);
            if (status != Status::Ok) {
                break;
            }
            resp(modbus_byte_hi(address));
            resp(modbus_byte_lo(address));
            resp(modbus_byte_hi(count));
            resp(modbus_byte_lo(count));
        }
    } break;
    default:
        status = Status::IllegalFunction;
    }

    switch (status) {
    case Status::Ok:
        // Everything is set up for being sent.
        break;
    case Status::Ignore:
        // Do _not_ send any answer. Just throw it away.
        return {};
    default:
        response_buffer[1] |= std::byte { 0x80 };
        response = response_buffer.begin() + 2;
        resp(std::byte { static_cast<uint8_t>(status) });
    }

    uint16_t crc = compute_crc(std::span { response_buffer.begin(), response });
    resp(modbus_byte_lo(crc));
    resp(modbus_byte_hi(crc));
    return { response_buffer.begin(), response };
}

} // namespace modbus
