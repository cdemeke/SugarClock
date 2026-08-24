#ifndef SEMVER_H
#define SEMVER_H

#include <stdint.h>
#include <stddef.h>

struct SemVer {
    int major;
    int minor;
    int patch;
    bool valid;

    SemVer() : major(0), minor(0), patch(0), valid(false) {}
    SemVer(int maj, int min, int pat) : major(maj), minor(min), patch(pat), valid(true) {}

    static SemVer parse(const char* str);
    int compare(const SemVer& other) const;

    bool operator<(const SemVer& other) const { return compare(other) < 0; }
    bool operator>(const SemVer& other) const { return compare(other) > 0; }
    bool operator<=(const SemVer& other) const { return compare(other) <= 0; }
    bool operator>=(const SemVer& other) const { return compare(other) >= 0; }
    bool operator==(const SemVer& other) const { return compare(other) == 0; }
    bool operator!=(const SemVer& other) const { return compare(other) != 0; }

    void toString(char* buf, size_t max_len) const;
};

#endif // SEMVER_H
