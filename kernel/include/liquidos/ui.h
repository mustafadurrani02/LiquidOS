#ifndef LIQUIDOS_UI_H
#define LIQUIDOS_UI_H

#include <liquidos/boot.h>
#include <liquidos/input.h>

void ui_init(const BootInfo *boot);
void ui_handle_event(const InputEvent *event);
void ui_update(u64 tick_count);
void ui_render(void);

#endif
