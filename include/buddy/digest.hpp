/// @file
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace buddy {

using Digest = std::span<std::byte, 32>;

/// Compute digest of the file referenced by file descriptor fd with given salt.
/// Returns true on success, false otherwise.
/// Note: This function doesn't preserve the file offset.
bool compute_file_digest(const int fd, const uint32_t salt, Digest);

} // namespace buddy
