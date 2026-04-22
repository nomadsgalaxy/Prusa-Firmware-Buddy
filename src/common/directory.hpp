/// @file
#pragma once

#include <dirent.h>

/// Represents a directory on the filesystem.
class Directory {
private:
    DIR *dir = nullptr;

public:
    /// Constructs empty directory object.
    Directory() = default;

    /// Constructs directory object, attempting to open the directory at `path`.
    /// If that fails, directory object is constructed empty.
    explicit Directory(const char *path);

    /// Destroys directory object, closing the directory if the object is not empty.
    ~Directory();

    Directory(const Directory &) = delete;
    Directory &operator=(const Directory &) = delete;

    Directory(Directory &&);
    Directory &operator=(Directory &&);

    /// Checks if the directory object is not empty.
    explicit operator bool() const;

    /// Closes the directory, leaving this object in empty state.
    void close();

    /// Reads directory entry at the current position and advances the position.
    /// May return nullptr.
    struct dirent *read();

    /// Set the position of the next read()
    void seek(long position);

    /// Get the position of the next read()
    long tell() const;
};
