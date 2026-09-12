#include "libre_patient.h"
#include <string.h>

LibrePatientSnapshot::LibrePatientSnapshot(const char* email, size_t count) : people(count) {
    strncpy(account, email, sizeof(account) - 1);
    account[sizeof(account) - 1] = '\0';
}

LibrePatients LibrePatientCache::get(const char* account) {
    LibrePatients snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = current_;
    }
    return snapshot && strcmp(snapshot->account, account) == 0 ? snapshot : nullptr;
}

void LibrePatientCache::publish(LibrePatients next) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_.swap(next);
    }
    // The previous snapshot is freed here only after its last reader finishes.
}

int libre_patient_index(const LibrePatient* patients, size_t count, const char* selected_id) {
    for (size_t i = 0; i < count; ++i) {
        if (!patients[i].id[0]) return LIBRE_PATIENT_INVALID;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(patients[i].id, patients[j].id) == 0) return LIBRE_PATIENT_INVALID;
        }
    }
    if (selected_id && selected_id[0]) {
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(patients[i].id, selected_id) == 0) return (int)i;
        }
        return LIBRE_PATIENT_MISSING;
    }
    if (count == 1) return 0;
    return count == 0 ? LIBRE_PATIENT_MISSING : LIBRE_PATIENT_AMBIGUOUS;
}
