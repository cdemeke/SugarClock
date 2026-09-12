#ifndef LIBRE_PATIENT_H
#define LIBRE_PATIENT_H
#include <stddef.h>

constexpr size_t LIBRE_MAX_PATIENTS = 16;
struct LibrePatient {
    char id[64];
    char name[128];
};

// A single connection may be bound automatically. Multiple connections require
// explicit selection; a missing saved ID must never fall back to another person.
enum { LIBRE_PATIENT_MISSING = -1, LIBRE_PATIENT_AMBIGUOUS = -2, LIBRE_PATIENT_INVALID = -3 };
int libre_patient_index(const LibrePatient* patients, size_t count, const char* selected_id);
#endif
