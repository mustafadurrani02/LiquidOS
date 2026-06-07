#include <liquidos/input.h>

#define INPUT_QUEUE_CAPACITY 256

static volatile u32 head = 0;
static volatile u32 tail = 0;
static InputEvent queue[INPUT_QUEUE_CAPACITY];

void input_queue_init(void) {
    head = 0;
    tail = 0;
}

bool input_queue_push(const InputEvent *event) {
    if (!event || event->type == INPUT_EVENT_NONE) {
        return false;
    }

    u32 next = (head + 1) % INPUT_QUEUE_CAPACITY;
    if (next == tail) {
        return false;
    }

    queue[head] = *event;
    head = next;
    return true;
}

bool input_queue_pop(InputEvent *event) {
    if (!event || tail == head) {
        return false;
    }

    *event = queue[tail];
    tail = (tail + 1) % INPUT_QUEUE_CAPACITY;
    return true;
}
