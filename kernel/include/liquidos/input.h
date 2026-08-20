#ifndef LIQUIDOS_INPUT_H
#define LIQUIDOS_INPUT_H

#include <liquidos/types.h>

typedef enum InputEventType {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY,
    INPUT_EVENT_MOUSE,
    INPUT_EVENT_TICK
} InputEventType;

typedef struct InputEvent {
    InputEventType type;
    char ch;
    i32 dx;
    i32 dy;
    bool left_down;
    bool right_down;
    bool middle_down;
} InputEvent;

void input_queue_init(void);
bool input_queue_push(const InputEvent *event);
bool input_queue_pop(InputEvent *event);
u64 input_queue_coalesced_count(void);
u64 input_queue_dropped_count(void);

#endif
