// Stub for chamber_filtration for unit testing
#include <feature/chamber_filtration/chamber_filtration.hpp>

namespace buddy {

ChamberFiltration &chamber_filtration() {
    static ChamberFiltration instance;
    return instance;
}

ChamberFiltrationBackend ChamberFiltration::backend() const {
    return ChamberFiltrationBackend::none;
}

} // namespace buddy
