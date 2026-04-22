/// @file
#include "cyphal_application.hpp"

#include "cyphal_application_impl.hpp"

namespace cyphal {
static bool equal(Bytes b, std::string_view s) {
    return std::ranges::equal(b, as_bytes(std::span { s.begin(), s.end() }));
}

NodeName parse_node_name(Bytes raw) {
    using namespace std::literals;
    if (equal(raw, "cz.prusa3d.honeybee.ac_controller"sv)) {
        return NodeName::cz_prusa3d_honeybee_ac_controller;
    }
    return NodeName::none;
}

} // namespace cyphal

cyphal::Application &cyphal::application() {
    static cyphal::ApplicationImpl instance;
    return instance;
}
