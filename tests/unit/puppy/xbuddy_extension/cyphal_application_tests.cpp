#include <cyphal_application.hpp>

#include "cyphal_presentation.hpp"
#include <cyphal_application_impl.hpp>
#include <master_activity.hpp>
#include <catch2/catch.hpp>
#include <vector>

// This is needed for linking but we don't need the actual implementation in tests
void cyphal::Application::log_from_app([[maybe_unused]] std::string_view s) {
}

using Application = cyphal::ApplicationImpl;
using cyphal::Command;
using cyphal::Health;
using cyphal::Heartbeat;
using cyphal::Mode;
using cyphal::NodeId;
using cyphal::Presentation;
using cyphal::Severity;
using cyphal::TimePoint;
using cyphal::UniqueId;

std::vector<uint8_t> mock_firmware_gen() {
    constexpr size_t firmware_size = 10 * 256;
    std::vector<uint8_t> result;
    for (size_t i = 0; i < firmware_size; i++) {
        result.push_back(random());
    }
    return result;
}

std::array<std::byte, 32> generate_random_digest() {
    std::array<std::byte, 32> result;
    for (auto &b : result) {
        b = std::byte(random());
    }
    return result;
}

uint32_t hal::rng::get() {
    return 0;
}

// Pretend that this is a firmware for the node.
//
// We don't want some all-zeroes or predictable pattern - that one could hide
// some reorderings or repetitions of parts or other such mistakes in transfer;
// random "noise" is likely to show them.
const std::vector<uint8_t> mock_firmware = mock_firmware_gen();

struct PnpAllocation {
    UniqueId unique_id;
    NodeId node_id;
    constexpr auto operator<=>(const PnpAllocation &) const = default;
};

struct ExecuteCommandRequest {
    NodeId node_id;
    Command command;
    std::vector<std::byte> parameter;
    constexpr auto operator<=>(const ExecuteCommandRequest &) const = default;
};

namespace cyphal {
std::ostream &operator<<(std::ostream &os, const NodeId &node_id) {
    return os << (int)(uint8_t)node_id;
}
std::ostream &operator<<(std::ostream &os, const Command &command) {
    switch (command) {
    case Command::start_app:
        return os << "start_app";
    case Command::get_app_salted_hash:
        return os << "get_app_salted_hash";
    case Command::software_update:
        return os << "software_update";
    case Command::restart:
        return os << "restart";
    }
    return os << "invalid";
}
} // namespace cyphal

std::ostream &operator<<(std::ostream &os, const ExecuteCommandRequest &request) {
    os << "{ " << request.node_id << ", " << request.command << ", {";
    for (const auto &byte : request.parameter) {
        os << (int)byte << ", ";
    }
    return os << "} }";
}

class MockPresentation final : public Presentation {
public:
    std::vector<PnpAllocation> pnp_allocation;
    std::vector<NodeId> node_get_info_request;
    uint32_t last_uptime = 0;
    uint32_t heartbeat_count = 0;
    bool last_heartbeat_healthy = true;

    std::vector<ExecuteCommandRequest> node_execute_command_request;

    struct FileResponse {
        NodeId remote_node_id;
        uint8_t transfer_id;
        std::vector<std::byte> data;

        constexpr auto operator<=>(const FileResponse &) const = default;
    };

    std::vector<FileResponse> file_responses;

    void transmit_heartbeat(uint32_t uptime, bool healthy) {
        last_uptime = uptime;
        ++heartbeat_count;
        last_heartbeat_healthy = healthy;
    }

    void transmit_pnp_allocation(const UniqueId &unique_id, NodeId node_id) {
        pnp_allocation.emplace_back(unique_id, node_id);
    }
    void transmit_diagnostic_record(Severity, const char *text) {
        WARN(text);
    }
    void transmit_node_get_info_request(NodeId remote_node_id) {
        node_get_info_request.emplace_back(remote_node_id);
    }
    void transmit_node_execute_command_request(NodeId remote_node_id, Command command, std::span<std::byte> parameter) {
        node_execute_command_request.emplace_back(remote_node_id, command, std::vector<std::byte> { parameter.begin(), parameter.end() });
    }
    void transmit_file_read_response(NodeId remote_node_id, uint8_t transfer_id, std::span<std::byte> data) {
        file_responses.push_back(FileResponse { remote_node_id, transfer_id, std::vector(data.begin(), data.end()) });
    }

    void transmit_ac_controller_config_request([[maybe_unused]] NodeId remote_node_id, [[maybe_unused]] const ac_controller::Config &) {
        abort();
    }

    void transmit_ac_controller_leds_config_request([[maybe_unused]] NodeId, [[maybe_unused]] const ac_controller::LedConfig &) {
        abort();
    }

    constexpr auto operator<=>(const MockPresentation &) const = default;
};

std::ostream &operator<<(std::ostream &os, const MockPresentation &mock) {
    os << "{ ";
    os << ".last_uptime = " << mock.last_uptime << ",";
    os << ".heartbeat_count = " << mock.heartbeat_count << ",";
    os << ".node_execute_command_request = {";
    for (const auto &x : mock.node_execute_command_request) {
        os << x << ",";
    }
    os << "}, ";
    os << ".node_get_info_request = {";
    for (const auto &x : mock.node_get_info_request) {
        os << x << ",";
    }
    os << "}, ";
    os << " }";
    return os;
}

UniqueId make_unique_id(uint32_t x) {
    union {
        uint8_t u8[16];
        uint32_t u32[4];
    } data;
    data.u32[0] = x;
    data.u32[1] = data.u32[2] = data.u32[3] = 0;
    return UniqueId { data.u8 };
};

TimePoint make_timepoint(uint32_t ms) {
    return TimePoint { std::chrono::milliseconds { ms } };
}

MockPresentation run_without_master_activity(Application &app, MockPresentation &mock, TimePoint timepoint) {
    auto mock_before = mock;
    while (app.step(mock, timepoint)) {
    }
    return mock_before;
}

MockPresentation run(Application &app, MockPresentation &mock, TimePoint timepoint) {
    master_activity.fetch_add(1);
    return run_without_master_activity(app, mock, timepoint);
}

const auto known_node_name = [] {
    std::string_view name { "cz.prusa3d.honeybee.ac_controller" };
    return as_bytes(std::span { name.data(), name.size() });
}();

const auto unknown_node_name = [] {
    std::string_view name { "cz.prusa3d.honeybee.unknown" };
    return as_bytes(std::span { name.data(), name.size() });
}();

SCENARIO("heart beats") {
    GIVEN("initial application state") {
        MockPresentation mock;
        Application app;

        THEN("there are no heartbeats") {
            CHECK(mock.heartbeat_count == 0);
        }

        WHEN("application runs") {
            run(app, mock, make_timepoint(1200));

            THEN("heartbeat is transmitted") {
                CHECK(mock.heartbeat_count == 1);
                CHECK(mock.last_uptime == 1);
                CHECK(mock.last_heartbeat_healthy);

                AND_WHEN("not enough time has passed") {
                    run(app, mock, make_timepoint(1400));

                    THEN("another heartbeat is not transmitted") {
                        CHECK(mock.heartbeat_count == 1);
                        CHECK(mock.last_uptime == 1);
                    }
                }

                AND_WHEN("enough time has passed") {
                    run(app, mock, make_timepoint(4400));

                    THEN("another heartbeat is transmitted") {
                        CHECK(mock.heartbeat_count == 2);
                        CHECK(mock.last_uptime == 4);
                        CHECK(mock.last_heartbeat_healthy);
                    }
                }

                AND_WHEN("master activity stops") {
                    run_without_master_activity(app, mock, make_timepoint(4400));

                    THEN("heartbeat marked as warning is transmitted") {
                        CHECK(mock.heartbeat_count == 2);
                        CHECK(mock.last_uptime == 4);
                        CHECK_FALSE(mock.last_heartbeat_healthy);
                    }
                }
            }
        }
    }
}

SCENARIO("basic allocation") {
    GIVEN("initial application state") {
        MockPresentation mock;
        Application app;

        THEN("there are no allocation responses") {
            REQUIRE(mock.pnp_allocation.empty());
        }

        WHEN("anonymous node with some uuid requests node id") {
            app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
            run(app, mock, make_timepoint(0));

            THEN("node id is allocated for that uuid") {
                REQUIRE(mock.pnp_allocation.size() == 1);
                CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
                CHECK(mock.pnp_allocation.back().node_id == NodeId { 1 });
            }

            THEN("allocated node id is not 0") {
                REQUIRE(mock.pnp_allocation.size() == 1);
                CHECK(mock.pnp_allocation.back().node_id != NodeId { 0 });
            }

            AND_WHEN("the same uuid requests some node id") {
                app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
                run(app, mock, make_timepoint(1));

                THEN("same node id is allocated") {
                    REQUIRE(mock.pnp_allocation.size() == 2);
                    CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
                    CHECK(mock.pnp_allocation.back().node_id == NodeId { 1 });
                }
            }

            AND_WHEN("different uuid requests some node id") {
                app.receive_pnp_allocation(make_unique_id(0xcafebabe));
                run(app, mock, make_timepoint(1));

                THEN("it should allocate different node id") {
                    REQUIRE(mock.pnp_allocation.size() == 2);
                    CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xcafebabe));
                    CHECK(mock.pnp_allocation.back().node_id == NodeId { 2 });
                }
            }
        }
    }
}

SCENARIO("full allocation table") {
    GIVEN("maximum number of different allocation requests") {
        MockPresentation mock;
        Application app;
        const int max = 15;
        for (int i = 1; i <= max; ++i) {
            app.receive_pnp_allocation(make_unique_id(i * i));
        }
        run(app, mock, make_timepoint(0));

        THEN("there have been that many allocation responses") {
            REQUIRE(mock.pnp_allocation.size() == max);
            CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(max * max));
            CHECK(mock.pnp_allocation.back().node_id == NodeId { max });
        }

        WHEN("another different allocation is requested") {
            app.receive_pnp_allocation(make_unique_id(7));
            run(app, mock, make_timepoint(1));

            THEN("allocation response is not sent") {
                REQUIRE(mock.pnp_allocation.size() == max);
            }
        }
    }
}

SCENARIO("heartbeat response") {
    GIVEN("node id was requested") {
        MockPresentation mock;
        Application app;
        app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
        run(app, mock, make_timepoint(0));

        THEN("node id was allocated") {
            REQUIRE(mock.pnp_allocation.size() == 1);
            CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
            CHECK(mock.pnp_allocation.back().node_id == NodeId { 1 });
        }

        WHEN("heartbeat comes from that node id") {
            app.receive_node_heartbeat(NodeId { 1 }, make_timepoint(1), { Health::nominal, Mode::operational, 0 });
            run(app, mock, make_timepoint(1));

            THEN("node details are requested") {
                REQUIRE(mock.node_get_info_request.size() == 1);
                CHECK(mock.node_get_info_request.back() == NodeId { 1 });
            }

            AND_WHEN("another heartbeat comes") {
                app.receive_node_heartbeat(NodeId { 1 }, make_timepoint(2), { Health::nominal, Mode::operational, 0 });
                run(app, mock, make_timepoint(2));

                THEN("node info is not requested again") {
                    REQUIRE(mock.node_get_info_request.size() == 1);
                }
            }
        }

        WHEN("heartbeat comes from a different node id") {
            app.receive_node_heartbeat(NodeId { 2 }, make_timepoint(1), { Health::nominal, Mode::operational, 0 });
            run(app, mock, make_timepoint(1));

            THEN("heartbeat is ignored") {
                CHECK(mock.node_get_info_request.empty());
            }
        }

        WHEN("heartbeat comes from a very large node id") {
            app.receive_node_heartbeat(NodeId { 64 }, make_timepoint(1), { Health::nominal, Mode::operational, 0 });
            run(app, mock, make_timepoint(1));

            THEN("heartbeat is ignored") {
                CHECK(mock.node_get_info_request.empty());
            }
        }
    }
}

SCENARIO("happy case") {
    const std::vector<std::byte> salt = { (std::byte)0, (std::byte)0, (std::byte)0, (std::byte)0 };

    const auto lingering_bootloader = Heartbeat { Health::advisory, Mode::software_update, 0 };
    // Means no app, only the bootloader
    const auto empty_bootloader = Heartbeat { Health::warning, Mode::software_update, 0 };
    const auto healthy_firmware = Heartbeat { Health::nominal, Mode::operational, 0 };

    const auto node_id = NodeId { 1 };

    const auto start_app = ExecuteCommandRequest { node_id, Command::start_app };
    const auto restart = ExecuteCommandRequest { node_id, Command::restart };
    const auto get_app_salted_hash = ExecuteCommandRequest { node_id, Command::get_app_salted_hash, salt };
    static const std::string_view dummy_parameter_sv = "/path/to/fw";
    static const std::vector<std::byte> dummy_parameter { (std::byte *)dummy_parameter_sv.begin(), (std::byte *)dummy_parameter_sv.end() };
    const auto software_update = ExecuteCommandRequest { node_id, Command::software_update, dummy_parameter };

    GIVEN("node id was requested") {
        MockPresentation mock;
        Application app;

        auto test_flashing = [&]() {
            THEN("software update command is executed") {
                const auto now = make_timepoint(4000);
                const auto mock_before = run(app, mock, now);
                REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                CHECK(mock.node_execute_command_request.back() == software_update);

                // The bootloader wants a bit of a file.
                uint8_t id = 42;
                app.receive_file_read_request(node_id, now, id, 0);
                run(app, mock, make_timepoint(4001));
                auto modbus_request = app.request();
                CHECK(modbus_request.flash_request == cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller);
                CHECK(modbus_request.offset == 0);

                std::optional<size_t> last_sent = std::nullopt;
                uint32_t time = 4002;

                std::vector<uint8_t> data;

                while (data.size() < mock_firmware.size()) {
                    INFO("Timepoint " << time);
                    const size_t rest = mock_firmware.size() - modbus_request.offset;
                    constexpr size_t max_chunk = sizeof(xbuddy_extension::modbus::Chunk::data);
                    const size_t size = std::min(rest, max_chunk);
                    bool progress = false;
                    if (last_sent != modbus_request.offset) {
                        last_sent = modbus_request.offset;
                        CHECK(app.receive_chunk(mock_firmware.data() + modbus_request.offset, size, modbus_request.offset + size == mock_firmware.size(), static_cast<uint16_t>(modbus_request.flash_request), modbus_request.offset));
                        progress = true;
                    }

                    size_t old_offset = modbus_request.offset;

                    const auto now = make_timepoint(time);
                    run(app, mock, now);

                    // Note: Node sends only one request, so we send only one response.
                    REQUIRE(mock.file_responses.size() <= 1);
                    if (!mock.file_responses.empty()) {
                        const auto &response = mock.file_responses[0];
                        CHECK(response.remote_node_id == node_id);
                        CHECK(response.transfer_id == id);

                        for (auto byte : response.data) {
                            data.push_back(static_cast<uint8_t>(byte));
                        }

                        mock.file_responses.clear();

                        if (data.size() < mock_firmware.size()) {
                            id++;
                            app.receive_file_read_request(node_id, now, id, data.size());
                        }

                        progress = true;
                    }

                    REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                    modbus_request = app.request();
                    if (old_offset != modbus_request.offset) {
                        progress = true;
                    }

                    REQUIRE(progress);

                    time++;
                }

                CHECK(data == mock_firmware);
            }

            WHEN("Application boots and requests ID again") {
                app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
                run(app, mock, make_timepoint(7000));

                THEN("No new ID is assigned") {
                    CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
                    CHECK(mock.pnp_allocation.back().node_id == node_id);
                }
            }
        };

        app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
        run(app, mock, make_timepoint(0));

        THEN("node id was allocated") {
            REQUIRE(mock.pnp_allocation.size() == 1);
            CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
            CHECK(mock.pnp_allocation.back().node_id == node_id);
        }

        WHEN("node sends heartbeat from lingering bootloader") {
            app.receive_node_heartbeat(node_id, make_timepoint(1), lingering_bootloader);
            run(app, mock, make_timepoint(1));

            THEN("node info is requested") {
                REQUIRE(mock.node_get_info_request.size() == 1);
                CHECK(mock.node_get_info_request.back() == node_id);
            }

            AND_WHEN("unknown node info is received") {
                std::string_view name { "com.dot" };
                app.receive_node_get_info_response(node_id, as_bytes(std::span { name.data(), name.size() }));
                run(app, mock, make_timepoint(2));

                THEN("no command is executed") {
                    CHECK(mock.node_execute_command_request.size() == 0);
                }
            }

            AND_WHEN("known node info is received") {
                app.receive_node_get_info_response(node_id, known_node_name);
                run(app, mock, make_timepoint(2));

                THEN("start_app command is executed") {
                    REQUIRE(mock.node_execute_command_request.size() == 1);
                    CHECK(mock.node_execute_command_request.back() == start_app);
                    const auto modbus_request = app.request();
                    CHECK(modbus_request.hash_request == cyphal::FirmwareFile::none);

                    AND_WHEN("nothing happens right away") {
                        const auto mock_before = run(app, mock, make_timepoint(3));

                        THEN("nothing happens") {
                            CHECK(mock == mock_before);
                        }
                    }

                    AND_WHEN("nothing happens for a long time") {
                        const auto mock_before = run(app, mock, make_timepoint(1035));

                        THEN("start_app command is retried") {
                            REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                            CHECK(mock.node_execute_command_request.back() == start_app);
                        }
                    }

                    AND_WHEN("node sends heartbeat from firmware") {
                        app.receive_node_heartbeat(node_id, make_timepoint(3), healthy_firmware);
                        const auto mock_before = run(app, mock, make_timepoint(3));

                        THEN("application requests file hash from parent system") {
                            CHECK(mock == mock_before);
                            const auto modbus_request = app.request();
                            CHECK(modbus_request.hash_request == cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller);

                            AND_WHEN("parent system sends file hash to application") {
                                std::byte digest[32] = {};
                                (void)app.receive_digest(cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller, 0, digest);
                                app.receive_node_heartbeat(node_id, make_timepoint(1000), healthy_firmware);
                                app.receive_node_heartbeat(node_id, make_timepoint(2000), healthy_firmware);
                                const auto mock_before = run(app, mock, make_timepoint(2500));

                                THEN("application requests hash from node") {
                                    REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                                    CHECK(mock.node_execute_command_request.back() == get_app_salted_hash);

                                    AND_WHEN("node responds with the matching hash") {
                                        app.receive_node_execute_command_response(node_id, 0, digest);
                                        const auto mock_before = run(app, mock, make_timepoint(3000));

                                        THEN("what happens?") {
                                            CHECK(mock == mock_before);
                                        }
                                    }

                                    AND_WHEN("node responds with mismatching hash") {
                                        std::byte wrong_digest[32] = {};
                                        wrong_digest[6] = (std::byte)'K';
                                        app.receive_node_execute_command_response(node_id, 0, wrong_digest);
                                        const auto mock_before = run(app, mock, make_timepoint(3000));

                                        THEN("restart command is executed") {
                                            REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                                            CHECK(mock.node_execute_command_request.back() == restart);

                                            AND_WHEN("new heartbeat from bootloader is received") {
                                                app.receive_node_heartbeat(node_id, make_timepoint(3001), lingering_bootloader);
                                                test_flashing();
                                            }

                                            AND_WHEN("no heartbeat is sent") {
                                                const auto mock_before = run(app, mock, make_timepoint(4000));
                                                THEN("we send no other command") {
                                                    CHECK(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size());
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        WHEN("Node sends heartbeat from empty bootloader") {
            app.receive_node_heartbeat(node_id, make_timepoint(1), empty_bootloader);
            run(app, mock, make_timepoint(1));

            THEN("node info is requested") {
                REQUIRE(mock.node_get_info_request.size() == 1);
                CHECK(mock.node_get_info_request.back() == node_id);

                AND_WHEN("known node info is received") {
                    app.receive_node_get_info_response(node_id, known_node_name);
                    run(app, mock, make_timepoint(2));

                    test_flashing();
                }
            }
        }

        WHEN("bootloader found valid app, started the app and the app sent a heartbeat") {
            app.receive_node_heartbeat(node_id, make_timepoint(1), healthy_firmware);
            run(app, mock, make_timepoint(1));

            THEN("node info is requested") {
                REQUIRE(mock.node_get_info_request.size() == 1);
                CHECK(mock.node_get_info_request.back() == node_id);

                AND_WHEN("known node info is received") {
                    app.receive_node_get_info_response(node_id, known_node_name);
                    const auto mock_before = run(app, mock, make_timepoint(2));

                    THEN("application requests hash from node and parent system") {
                        REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                        CHECK(mock.node_execute_command_request.back() == get_app_salted_hash);
                        const auto modbus_request = app.request();
                        CHECK(modbus_request.hash_request == cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller);

                        AND_WHEN("they respond with matching hash") {
                            const auto digest = generate_random_digest();
                            (void)app.receive_digest(cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller, 0, digest);
                            app.receive_node_execute_command_response(node_id, 0, digest);
                            const auto mock_before = run(app, mock, make_timepoint(3));

                            THEN("nothing happens anymore") {
                                CHECK(mock == mock_before);
                            }
                        }

                        AND_WHEN("they respond with mismatching hash") {
                            const auto digest1 = generate_random_digest();
                            const auto digest2 = generate_random_digest();
                            (void)app.receive_digest(cyphal::FirmwareFile::cz_prusa3d_honeybee_ac_controller, 0, digest1);
                            app.receive_node_execute_command_response(node_id, 0, digest2);
                            const auto mock_before = run(app, mock, make_timepoint(3));

                            THEN("restart command is executed") {
                                REQUIRE(mock.node_execute_command_request.size() == mock_before.node_execute_command_request.size() + 1);
                                CHECK(mock.node_execute_command_request.back() == restart);

                                AND_WHEN("node id was requested") {
                                    app.receive_pnp_allocation(make_unique_id(0xdeadbeef));
                                    const auto mock_before = run(app, mock, make_timepoint(4));

                                    THEN("node id was allocated") {
                                        REQUIRE(mock.pnp_allocation.size() == mock_before.pnp_allocation.size() + 1);
                                        CHECK(mock.pnp_allocation.back().unique_id == make_unique_id(0xdeadbeef));
                                        CHECK(mock.pnp_allocation.back().node_id == node_id);

                                        AND_WHEN("node sends heartbeat from lingering bootloader") {
                                            app.receive_node_heartbeat(node_id, make_timepoint(5), lingering_bootloader);
                                            const auto mock_before = run(app, mock, make_timepoint(5));

                                            test_flashing();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                AND_WHEN("unknown node info is received") {
                    app.receive_node_get_info_response(node_id, unknown_node_name);
                    run(app, mock, make_timepoint(2));

                    // TODO BFW-7918
                    // Check that no communication happens with that node...
                }
            }
        }
    }
}
