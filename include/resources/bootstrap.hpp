#pragma once

#include "resources/revision.hpp"

namespace buddy::resources {

bool has_resources(const Revision &revision);

bool bootstrap(const Revision &revision);

}; // namespace buddy::resources
