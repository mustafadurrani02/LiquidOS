#ifndef LIQUIDOS_UI_H
#define LIQUIDOS_UI_H

#include <liquidos/boot.h>
#include <liquidos/input.h>

typedef struct UiPerformanceStats {
    u64 render_calls;
    u64 presented_frames;
    u64 full_redraws;
    u64 cursor_presents;
    u64 dirty_pixels;
} UiPerformanceStats;

void ui_init(const BootInfo *boot);
void ui_handle_event(const InputEvent *event);
void ui_update(u64 tick_count);
void ui_render(void);
UiPerformanceStats ui_performance_stats(void);

#endif
