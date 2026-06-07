#ifndef LIQUIDOS_SERIAL_H
#define LIQUIDOS_SERIAL_H

#include <liquidos/types.h>

void serial_init(void);
void serial_write_char(char ch);
void serial_write(const char *text);
void serial_write_line(const char *text);
void serial_write_hex(u64 value);

#endif
