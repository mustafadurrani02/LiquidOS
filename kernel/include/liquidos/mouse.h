#ifndef LIQUIDOS_MOUSE_H
#define LIQUIDOS_MOUSE_H

#include <liquidos/input.h>
#include <liquidos/types.h>

void mouse_init_packet_state(void);
bool mouse_handle_byte(u8 value, InputEvent *event);

#endif
