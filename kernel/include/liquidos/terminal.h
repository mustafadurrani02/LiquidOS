#ifndef LIQUIDOS_TERMINAL_H
#define LIQUIDOS_TERMINAL_H

#include <liquidos/types.h>

void terminal_init(void);
void terminal_on_char(char ch);
void terminal_render(i32 x, i32 y, i32 width, i32 height, bool focused);

#endif
