#ifndef LIBRE_PATIENT_H
#define LIBRE_PATIENT_H
#include <stddef.h>
#include <memory>
#include <mutex>
#include <vector>

constexpr size_t LIBRE_MAX_PATIENTS = 16;
struct LibrePatient {
    char id[64];
    char name[128];
};

struct LibrePatientSnapshot {
    char account[64];
    std::vector<LibrePatient> people;
    LibrePatientSnapshot(const char* email, size_t count);
};
using LibrePatients = std::shared_ptr<const LibrePatientSnapshot>;

// Readers retain an immutable snapshot after releasing the blocking mutex.
// On ESP32 std::mutex uses the FreeRTOS-backed pthread mutex implementation.
// Allocation, serialization, account comparison, and destruction happen outside
// the lock; only the shared pointer is copied/swapped while holding it.
class LibrePatientCache {
public:
    LibrePatients get(const char* account);
    void publish(LibrePatients next);
    void clear() { publish(nullptr); }
private:
    std::mutex mutex_;
    LibrePatients current_;
};

// A single connection may be bound automatically. Multiple connections require
// explicit selection; a missing saved ID must never fall back to another person.
enum { LIBRE_PATIENT_MISSING = -1, LIBRE_PATIENT_AMBIGUOUS = -2, LIBRE_PATIENT_INVALID = -3 };
int libre_patient_index(const LibrePatient* patients, size_t count, const char* selected_id);
#endif
