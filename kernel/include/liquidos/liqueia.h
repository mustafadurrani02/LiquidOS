#ifndef LIQUIDOS_LIQUEIA_H
#define LIQUIDOS_LIQUEIA_H

#include <liquidos/types.h>

void liqueia_init(void);
void liqueia_on_char(char ch);
void liqueia_handle_click(i32 x, i32 y, i32 width, i32 height, i32 mouse_x, i32 mouse_y);
void liqueia_render(i32 x, i32 y, i32 width, i32 height);

#endif
