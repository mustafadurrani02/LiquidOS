#include <liquidos/input.h>

#define INPUT_QUEUE_CAPACITY 256

static volatile u32 head = 0;
static volatile u32 tail = 0;
static InputEvent queue[INPUT_QUEUE_CAPACITY];
static u64 coalesced_events = 0;
static u64 dropped_events = 0;

void input_queue_init(void) {
    head = 0;
    tail = 0;
    coalesced_events = 0;
    dropped_events = 0;
}

static bool same_mouse_buttons(const InputEvent *left, const InputEvent *right) {
    return left->left_down == right->left_down &&
           left->right_down == right->right_down &&
           left->middle_down == right->middle_down;
}

bool input_queue_push(const InputEvent *event) {
    if (!event || event->type == INPUT_EVENT_NONE) {
        return false;
    }

    if (event->type == INPUT_EVENT_MOUSE && head != tail) {
        u32 previous = head == 0 ? INPUT_QUEUE_CAPACITY - 1 : head - 1;
        InputEvent *last = &queue[previous];
        if (last->type == INPUT_EVENT_MOUSE && same_mouse_buttons(last, event)) {
            last->dx += event->dx;
            last->dy += event->dy;
            coalesced_events++;
            return true;
        }
    }

    u32 next = (head + 1) % INPUT_QUEUE_CAPACITY;
    if (next == tail) {
        dropped_events++;
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

u64 input_queue_coalesced_count(void) {
    return coalesced_events;
}

u64 input_queue_dropped_count(void) {
    return dropped_events;
}
