#pragma once

#include <cstdint>
#include <guiconfig/guiconfig.h>

/// Sizes of the thumbnail images expected in gcodes.
///
/// Equivalence checked by static asserts in GuiDefaults.hpp
namespace thumbnail_sizes {
#if HAS_MINI_DISPLAY() || HAS_MOCK_DISPLAY()
constexpr uint16_t progress_thumbnail_width = 240;
constexpr uint16_t old_progress_thumbnail_width = 200;
constexpr uint16_t progress_thumbnail_height = 240;
constexpr uint16_t preview_thumbnail_width = 220;
constexpr uint16_t preview_thumbnail_height = 124;
#elif HAS_LARGE_DISPLAY()
constexpr uint16_t progress_thumbnail_width = 480;
constexpr uint16_t old_progress_thumbnail_width = 440;
constexpr uint16_t progress_thumbnail_height = 240;
constexpr uint16_t preview_thumbnail_width = 313;
constexpr uint16_t preview_thumbnail_height = 173;
#endif
} // namespace thumbnail_sizes
