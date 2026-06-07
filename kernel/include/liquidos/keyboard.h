#ifndef LIQUIDOS_KEYBOARD_H
#define LIQUIDOS_KEYBOARD_H

#include <liquidos/input.h>
#include <liquidos/types.h>

void keyboard_init(void);
bool keyboard_handle_scancode(u8 scancode, InputEvent *event);

#endif
