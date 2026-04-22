/// @file
#include <common/directory.hpp>

#include <dirent.h>
#include <utility>

Directory::Directory(const char *path)
    : dir { opendir(path) } {}

Directory::~Directory() {
    close();
}

Directory::Directory(Directory &&other) {
    dir = other.dir;
    other.dir = nullptr;
}

Directory &Directory::operator=(Directory &&other) {
    using std::swap;
    swap(dir, other.dir);
    return *this;
}

Directory::operator bool() const {
    return dir;
}

void Directory::close() {
    (void)closedir(dir);
    dir = nullptr;
}

struct dirent *Directory::read() {
    return readdir(dir);
}

void Directory::seek(long position) {
    seekdir(dir, position);
}

long Directory::tell() const {
    return telldir(dir);
}
