#ifndef LIQUIDOS_PLATFORM_H
#define LIQUIDOS_PLATFORM_H

#include <liquidos/types.h>

typedef enum PlatformStatus {
    PLATFORM_AVAILABLE = 0,
    PLATFORM_PARTIAL,
    PLATFORM_PLANNED,
    PLATFORM_MISSING
} PlatformStatus;

typedef struct PlatformCapability {
    const char *area;
    const char *name;
    PlatformStatus status;
    const char *detail;
} PlatformCapability;

typedef struct PlatformSummary {
    size_t available;
    size_t partial;
    size_t planned;
    size_t missing;
} PlatformSummary;

void platform_init(void);
size_t platform_capability_count(void);
const PlatformCapability *platform_capability_get(size_t index);
const char *platform_status_name(PlatformStatus status);
PlatformSummary platform_summary(void);

#endif
