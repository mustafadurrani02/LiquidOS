#include <liquidos/lib.h>

void u64_to_dec(u64 value, char *out, size_t out_size) {
    char temp[32];
    size_t used = 0;

    if (out_size == 0) {
        return;
    }

    if (value == 0) {
        if (out_size > 1) {
            out[0] = '0';
            out[1] = 0;
        } else {
            out[0] = 0;
        }
        return;
    }

    while (value > 0 && used < sizeof(temp)) {
        temp[used++] = (char)('0' + (value % 10));
        value /= 10;
    }

    size_t written = 0;
    while (used > 0 && written + 1 < out_size) {
        out[written++] = temp[--used];
    }
    out[written] = 0;
}

void u64_to_hex(u64 value, char *out, size_t out_size) {
    static const char digits[] = "0123456789ABCDEF";

    if (out_size == 0) {
        return;
    }

    if (out_size < 3) {
        out[0] = 0;
        return;
    }

    out[0] = '0';
    out[1] = 'x';

    size_t pos = 2;
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((value >> shift) & 0xF);
        if (nibble != 0 || started || shift == 0) {
            started = true;
            if (pos + 1 < out_size) {
                out[pos++] = digits[nibble];
            }
        }
    }
    out[pos] = 0;
}
