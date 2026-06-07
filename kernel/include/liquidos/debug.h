#ifndef LIQUIDOS_DEBUG_H
#define LIQUIDOS_DEBUG_H

#include <liquidos/types.h>

void panic(const char *message);
void panic_at(const char *message, const char *file, u32 line);

#define KASSERT(condition) \
    do { \
        if (!(condition)) { \
            panic_at("Assertion failed: " #condition, __FILE__, __LINE__); \
        } \
    } while (0)

#endif
