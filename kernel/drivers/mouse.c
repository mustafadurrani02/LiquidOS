#include <liquidos/mouse.h>

static u8 packet[3];
static u8 packet_index = 0;

void mouse_init_packet_state(void) {
    packet_index = 0;
}

static i32 accelerate_delta(i32 delta) {
    i32 sign = delta < 0 ? -1 : 1;
    i32 magnitude = delta < 0 ? -delta : delta;

    if (magnitude == 0) {
        return 0;
    }

    i32 scaled = magnitude * 3;
    if (magnitude > 4) {
        scaled += (magnitude - 4) * 2;
    }
    if (magnitude > 16) {
        scaled += (magnitude - 16) * 2;
    }
    if (scaled > 96) {
        scaled = 96;
    }

    return scaled * sign;
}

bool mouse_handle_byte(u8 value, InputEvent *event) {
    event->type = INPUT_EVENT_NONE;

    if (packet_index == 0 && (value & 0x08) == 0) {
        return false;
    }

    packet[packet_index++] = value;
    if (packet_index < 3) {
        return false;
    }

    packet_index = 0;

    i32 dx = (i32)packet[1];
    i32 dy = (i32)packet[2];

    if (packet[0] & 0x10) {
        dx |= ~0xFF;
    }
    if (packet[0] & 0x20) {
        dy |= ~0xFF;
    }

    event->type = INPUT_EVENT_MOUSE;
    event->dx = accelerate_delta(dx);
    event->dy = accelerate_delta(-dy);
    event->left_down = (packet[0] & 0x01) != 0;
    event->right_down = (packet[0] & 0x02) != 0;
    event->middle_down = (packet[0] & 0x04) != 0;
    return true;
}
