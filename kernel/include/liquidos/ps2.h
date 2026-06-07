#ifndef LIQUIDOS_PS2_H
#define LIQUIDOS_PS2_H

#include <liquidos/input.h>
#include <liquidos/types.h>

void ps2_init(void);
bool ps2_poll(InputEvent *event);

#endif
