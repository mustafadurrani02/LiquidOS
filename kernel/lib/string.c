#include <liquidos/lib.h>

static bool string_pointer_valid(const char *text) {
    uintptr_t value = (uintptr_t)text;
    return value >= 0x1000 && value < 0x40000000ULL;
}

void *memcpy(void *dest, const void *src, size_t count) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    while (count >= sizeof(u64)) {
        *(u64 *)d = *(const u64 *)s;
        d += sizeof(u64);
        s += sizeof(u64);
        count -= sizeof(u64);
    }

    while (count > 0) {
        *d++ = *s++;
        count--;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    if (d == s || count == 0) {
        return dest;
    }

    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

void *memset(void *dest, int value, size_t count) {
    u8 *d = (u8 *)dest;
    u8 byte = (u8)value;
    u64 word = byte;
    word |= word << 8;
    word |= word << 16;
    word |= word << 32;

    while (count >= sizeof(u64)) {
        *(u64 *)d = word;
        d += sizeof(u64);
        count -= sizeof(u64);
    }

    while (count > 0) {
        *d++ = byte;
        count--;
    }
    return dest;
}

int memcmp(const void *left, const void *right, size_t count) {
    const u8 *l = (const u8 *)left;
    const u8 *r = (const u8 *)right;
    for (size_t i = 0; i < count; i++) {
        if (l[i] != r[i]) {
            return (int)l[i] - (int)r[i];
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    if (!string_pointer_valid(left) || !string_pointer_valid(right)) {
        return left == right ? 0 : (left < right ? -1 : 1);
    }
    while (*left && (*left == *right)) {
        left++;
        right++;
    }
    return (int)(u8)*left - (int)(u8)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i] || left[i] == 0 || right[i] == 0) {
            return (int)(u8)left[i] - (int)(u8)right[i];
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*dest++ = *src++) != 0) {
    }
    return out;
}

char *strncpy(char *dest, const char *src, size_t count) {
    size_t i = 0;
    for (; i < count && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < count; i++) {
        dest[i] = 0;
    }
    return dest;
}
