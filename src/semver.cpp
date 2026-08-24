#include "semver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

SemVer SemVer::parse(const char* str) {
    SemVer result;
    if (!str || *str == '\0') return result;

    // Must start with digit
    if (!isdigit((unsigned char)*str)) return result;
    if (*str == '0' && isdigit((unsigned char)str[1])) return result;

    char* endptr = NULL;
    long maj = strtol(str, &endptr, 10);
    if (!endptr || *endptr != '.' || maj < 0 || maj > 99999) return result;

    str = endptr + 1;
    if (!isdigit((unsigned char)*str)) return result;
    if (*str == '0' && isdigit((unsigned char)str[1])) return result;
    long min = strtol(str, &endptr, 10);
    if (!endptr || *endptr != '.' || min < 0 || min > 99999) return result;

    str = endptr + 1;
    if (!isdigit((unsigned char)*str)) return result;
    if (*str == '0' && isdigit((unsigned char)str[1])) return result;
    long pat = strtol(str, &endptr, 10);
    if (!endptr || *endptr != '\0' || pat < 0 || pat > 99999) {
        return result;
    }

    result.major = (int)maj;
    result.minor = (int)min;
    result.patch = (int)pat;
    result.valid = true;
    return result;
}

int SemVer::compare(const SemVer& other) const {
    if (!this->valid && !other.valid) return 0;
    if (!this->valid) return -1;
    if (!other.valid) return 1;

    if (this->major != other.major) {
        return (this->major > other.major) ? 1 : -1;
    }
    if (this->minor != other.minor) {
        return (this->minor > other.minor) ? 1 : -1;
    }
    if (this->patch != other.patch) {
        return (this->patch > other.patch) ? 1 : -1;
    }
    return 0;
}

void SemVer::toString(char* buf, size_t max_len) const {
    if (!buf || max_len == 0) return;
    if (!valid) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, max_len, "%d.%d.%d", major, minor, patch);
}
