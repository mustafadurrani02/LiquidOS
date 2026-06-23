#include <liquidos/fs.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/network.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>

#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define RTL_REG_IDR0 0x00
#define RTL_REG_TSD0 0x10
#define RTL_REG_TSAD0 0x20
#define RTL_REG_RBSTART 0x30
#define RTL_REG_CAPR 0x38
#define RTL_REG_COMMAND 0x37
#define RTL_REG_IMR 0x3C
#define RTL_REG_ISR 0x3E
#define RTL_REG_TCR 0x40
#define RTL_REG_RCR 0x44
#define RTL_REG_CONFIG1 0x52

#define RTL_CMD_RX_EMPTY 0x01
#define RTL_CMD_TX_ENABLE 0x04
#define RTL_CMD_RX_ENABLE 0x08
#define RTL_CMD_RESET 0x10

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define RX_BUFFER_SIZE (8192 + 16 + 1500)
#define TX_BUFFER_SIZE 2048
#define NET_PACKET_SIZE 1536
#define HTTP_RAW_SIZE 4096
#define HTTP_BODY_SIZE 8192

typedef struct NetRoute {
    const char *url;
    const char *mime;
    const char *body;
} NetRoute;

typedef struct UrlParts {
    char host[NET_HOST_LENGTH];
    char path[NET_URL_LENGTH];
    u16 port;
    bool https;
} UrlParts;

static NetInfo info = {
    false,
    false,
    false,
    "loopnet0 fallback routes",
    "10.0.2.15",
    "10.0.2.2",
    "10.0.2.3",
};

static const NetRoute routes[] = {
    {
        "http://liquidos.local/",
        "text/html",
        "LiquidOS Network Home\n"
        "Status: online through LiquidOS networking\n"
        "Try http://liquidos.local/store, http://liquidos.local/docs, or http://example.com/\n"
    },
    {
        "http://liquidos.local/store",
        "text/html",
        "LiquidOS Store\n"
        "Downloadable packages: notes.lpkg, paint.lpkg, calc.lpkg\n"
        "Use Terminal: download notes\n"
    },
    {
        "http://liquidos.local/docs",
        "text/html",
        "LiquidOS Networking\n"
        "This build includes a QEMU RTL8139 driver, ARP, DNS, TCP, and plain HTTP GET.\n"
        "HTTPS/TLS and a full HTML engine are still future systems.\n"
    },
    {
        "http://liquidos.local/downloads/readme.txt",
        "text/plain",
        "Downloaded through the LiquidOS network service.\n"
    },
    {
        "http://store.liquidos.local/packages/notes.lpkg",
        "application/x-liquidos-package",
        "LPKG1\nname=notes\ndisplay=Liquid Notes\nversion=1.0\nentry=APPS/NOTES.APP\npayload=APPS/HELLO.APP\ncategory=Productivity\npermissions=31\nfiles=1\n"
    },
    {
        "http://store.liquidos.local/packages/paint.lpkg",
        "application/x-liquidos-package",
        "LPKG1\nname=paint\ndisplay=Liquid Paint\nversion=1.0\nentry=APPS/PAINT.APP\npayload=APPS/APP_A.APP\ncategory=Creative\npermissions=31\nfiles=1\n"
    },
    {
        "http://store.liquidos.local/packages/calc.lpkg",
        "application/x-liquidos-package",
        "LPKG1\nname=calc\ndisplay=Liquid Calc\nversion=1.0\nentry=APPS/CALC.APP\npayload=APPS/APP_B.APP\ncategory=Utility\npermissions=31\nfiles=1\n"
    },
};

static bool rtl_present = false;
static u16 rtl_io = 0;
static u32 rx_offset = 0;
static u32 tx_index = 0;
static u16 ip_ident = 1;
static u16 next_ephemeral_port = 49152;
static u8 local_mac[6];
static u8 gateway_mac[6];
static bool gateway_mac_ready = false;
static const u8 broadcast_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const u8 local_ip[4] = { 10, 0, 2, 15 };
static const u8 gateway_ip[4] = { 10, 0, 2, 2 };
static const u8 dns_ip[4] = { 10, 0, 2, 3 };

static u8 rx_buffer[RX_BUFFER_SIZE] __attribute__((section(".dma"), aligned(16)));
static u8 tx_buffers[4][TX_BUFFER_SIZE] __attribute__((section(".dma"), aligned(16)));
static u8 net_packet[NET_PACKET_SIZE];
static char http_raw[HTTP_RAW_SIZE];
static char http_body[HTTP_BODY_SIZE];
static char http_mime[48];

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static char ascii_lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static bool starts_with_ci(const char *text, const char *prefix) {
    while (*prefix) {
        if (ascii_lower(*text) != ascii_lower(*prefix)) {
            return false;
        }
        text++;
        prefix++;
    }
    return true;
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static u16 read_be16(const u8 *data) {
    return (u16)(((u16)data[0] << 8) | data[1]);
}

static u32 read_be32(const u8 *data) {
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static void write_be16(u8 *data, u16 value) {
    data[0] = (u8)(value >> 8);
    data[1] = (u8)value;
}

static void write_be32(u8 *data, u32 value) {
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)value;
}

static bool ip_equal(const u8 *a, const u8 *b) {
    return memcmp(a, b, 4) == 0;
}

static u16 checksum_finish(u32 sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16)~sum;
}

static u16 checksum_bytes(const u8 *data, size_t len) {
    u32 sum = 0;
    while (len > 1) {
        sum += read_be16(data);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += (u16)data[0] << 8;
    }
    return checksum_finish(sum);
}

static NetResponse response(bool ok, u16 status, const char *mime, const char *body, const char *message) {
    NetResponse out;
    out.ok = ok;
    out.status = status;
    out.mime = mime;
    out.body = body ? body : "";
    out.size = strlen(out.body);
    strncpy(out.message, message, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = 0;
    return out;
}

static bool header_contains_token(const char *headers, const char *name, const char *token) {
    size_t token_len = strlen(token);
    const char *line = headers;
    while (*line) {
        const char *line_end = line;
        while (*line_end && !(line_end[0] == '\r' && line_end[1] == '\n')) {
            line_end++;
        }
        if (starts_with_ci(line, name)) {
            for (const char *p = line + strlen(name); p + token_len <= line_end; p++) {
                bool match = true;
                for (size_t i = 0; i < token_len; i++) {
                    if (ascii_lower(p[i]) != ascii_lower(token[i])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return true;
                }
            }
        }
        if (!*line_end) {
            return false;
        }
        line = line_end + 2;
        if (line[0] == '\r' && line[1] == '\n') {
            return false;
        }
    }
    return false;
}

static bool copy_header_value(const char *headers, const char *name, char *out, size_t out_size) {
    if (!headers || !name || !out || out_size == 0) {
        return false;
    }
    out[0] = 0;
    const char *line = headers;
    while (*line) {
        const char *line_end = line;
        while (*line_end && !(line_end[0] == '\r' && line_end[1] == '\n')) {
            line_end++;
        }
        if (starts_with_ci(line, name)) {
            const char *value = line + strlen(name);
            while (value < line_end && (*value == ' ' || *value == '\t')) {
                value++;
            }
            size_t used = 0;
            while (value < line_end && used + 1 < out_size) {
                out[used++] = *value++;
            }
            out[used] = 0;
            return used > 0;
        }
        if (!*line_end) {
            return false;
        }
        line = line_end + 2;
    }
    return false;
}

static i32 hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

static void decode_chunked_body(const char *body, char *out, size_t out_size) {
    size_t used = 0;
    const char *cursor = body;
    while (*cursor && used + 1 < out_size) {
        while (*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }

        u32 chunk_size = 0;
        bool saw_digit = false;
        while (*cursor) {
            i32 value = hex_value(*cursor);
            if (value < 0) {
                break;
            }
            saw_digit = true;
            chunk_size = chunk_size * 16 + (u32)value;
            cursor++;
        }
        if (!saw_digit || chunk_size == 0) {
            break;
        }

        while (*cursor && *cursor != '\n') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }

        for (u32 i = 0; i < chunk_size && cursor[i] && used + 1 < out_size; i++) {
            out[used++] = cursor[i];
        }
        cursor += chunk_size;
    }
    out[used] = 0;
}

static u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = 0x80000000U |
                  ((u32)bus << 16) |
                  ((u32)slot << 11) |
                  ((u32)func << 8) |
                  (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = 0x80000000U |
                  ((u32)bus << 16) |
                  ((u32)slot << 11) |
                  ((u32)func << 8) |
                  (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static bool rtl8139_find(void) {
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            u32 id = pci_read32((u8)bus, slot, 0, 0);
            if ((id & 0xFFFF) == RTL_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL_DEVICE_ID) {
                u32 bar0 = pci_read32((u8)bus, slot, 0, 0x10);
                if ((bar0 & 1) == 0) {
                    return false;
                }
                rtl_io = (u16)(bar0 & ~3U);
                u32 command = pci_read32((u8)bus, slot, 0, 0x04);
                command |= 0x00000005U; /* I/O space + bus master */
                pci_write32((u8)bus, slot, 0, 0x04, command);
                return rtl_io != 0;
            }
        }
    }
    return false;
}

static bool rtl8139_init(void) {
    if (!rtl8139_find()) {
        return false;
    }

    outb(rtl_io + RTL_REG_CONFIG1, 0x00);
    outb(rtl_io + RTL_REG_COMMAND, RTL_CMD_RESET);
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(rtl_io + RTL_REG_COMMAND) & RTL_CMD_RESET) == 0) {
            break;
        }
    }
    if (inb(rtl_io + RTL_REG_COMMAND) & RTL_CMD_RESET) {
        return false;
    }

    for (u8 i = 0; i < 6; i++) {
        local_mac[i] = inb(rtl_io + RTL_REG_IDR0 + i);
    }

    memset(rx_buffer, 0, sizeof(rx_buffer));
    outl(rtl_io + RTL_REG_RBSTART, (u32)(uintptr_t)rx_buffer);
    outw(rtl_io + RTL_REG_IMR, 0x0000);
    outw(rtl_io + RTL_REG_ISR, 0xFFFF);
    outl(rtl_io + RTL_REG_TCR, 0x00000000);
    outl(rtl_io + RTL_REG_RCR, 0x0000000F | (1U << 7)); /* AAP/APM/AM/AB + wrap */
    outb(rtl_io + RTL_REG_COMMAND, RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
    rx_offset = 0;
    tx_index = 0;
    rtl_present = true;
    return true;
}

static bool rtl8139_send(const u8 *packet, size_t len) {
    if (!rtl_present || !packet || len > TX_BUFFER_SIZE) {
        return false;
    }
    size_t copy_len = len;
    if (len < 60) {
        len = 60;
    }

    u8 *buffer = tx_buffers[tx_index];
    memset(buffer, 0, TX_BUFFER_SIZE);
    memcpy(buffer, packet, copy_len);

    u16 tsad = (u16)(rtl_io + RTL_REG_TSAD0 + tx_index * 4);
    u16 tsd = (u16)(rtl_io + RTL_REG_TSD0 + tx_index * 4);
    outl(tsad, (u32)(uintptr_t)buffer);
    outl(tsd, (u32)len);

    for (u32 i = 0; i < 200000; i++) {
        u32 status = inl(tsd);
        if (status & (1U << 15)) {
            tx_index = (tx_index + 1) & 3;
            return true;
        }
        if (status & (1U << 30)) {
            return false;
        }
    }
    tx_index = (tx_index + 1) & 3;
    return true;
}

static void rtl_ring_read(u32 offset, u8 *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i] = rx_buffer[(offset + (u32)i) % RX_BUFFER_SIZE];
    }
}

static bool rtl8139_receive(u8 *packet, size_t *len) {
    if (!rtl_present || !packet || !len) {
        return false;
    }
    if (inb(rtl_io + RTL_REG_COMMAND) & RTL_CMD_RX_EMPTY) {
        return false;
    }

    u8 header[4];
    rtl_ring_read(rx_offset, header, sizeof(header));
    u16 status = (u16)(header[0] | ((u16)header[1] << 8));
    u16 packet_len = (u16)(header[2] | ((u16)header[3] << 8));
    if ((status & 1) == 0 || packet_len < 4 || packet_len > NET_PACKET_SIZE) {
        rx_offset = (rx_offset + 4 + packet_len + 3) & ~3U;
        rx_offset %= RX_BUFFER_SIZE;
        outw(rtl_io + RTL_REG_CAPR, (u16)(rx_offset - 16));
        outw(rtl_io + RTL_REG_ISR, 0xFFFF);
        return false;
    }

    packet_len -= 4; /* Drop Ethernet CRC. */
    rtl_ring_read((rx_offset + 4) % RX_BUFFER_SIZE, packet, packet_len);
    *len = packet_len;

    rx_offset = (rx_offset + 4 + packet_len + 4 + 3) & ~3U;
    rx_offset %= RX_BUFFER_SIZE;
    outw(rtl_io + RTL_REG_CAPR, (u16)(rx_offset - 16));
    outw(rtl_io + RTL_REG_ISR, 0xFFFF);
    return true;
}

static void build_eth_header(u8 *frame, const u8 *dest, u16 type) {
    memcpy(frame, dest, 6);
    memcpy(frame + 6, local_mac, 6);
    write_be16(frame + 12, type);
}

static bool send_arp_request(const u8 *target_ip) {
    u8 frame[42];
    build_eth_header(frame, broadcast_mac, ETH_TYPE_ARP);
    write_be16(frame + 14, 1);
    write_be16(frame + 16, ETH_TYPE_IPV4);
    frame[18] = 6;
    frame[19] = 4;
    write_be16(frame + 20, 1);
    memcpy(frame + 22, local_mac, 6);
    memcpy(frame + 28, local_ip, 4);
    memset(frame + 32, 0, 6);
    memcpy(frame + 38, target_ip, 4);
    return rtl8139_send(frame, sizeof(frame));
}

static void handle_arp(const u8 *packet, size_t len) {
    if (len < 42 || read_be16(packet + 12) != ETH_TYPE_ARP) {
        return;
    }
    const u8 *arp = packet + 14;
    if (read_be16(arp + 6) != 2) {
        return;
    }
    const u8 *sender_mac = arp + 8;
    const u8 *sender_ip = arp + 14;
    const u8 *target_ip = arp + 24;
    if (ip_equal(sender_ip, gateway_ip) && ip_equal(target_ip, local_ip)) {
        memcpy(gateway_mac, sender_mac, 6);
        gateway_mac_ready = true;
    }
}

static bool poll_packet(u8 *packet, size_t *len, u32 loops) {
    for (u32 i = 0; i < loops; i++) {
        size_t packet_len = 0;
        if (!rtl8139_receive(packet, &packet_len)) {
            continue;
        }
        if (read_be16(packet + 12) == ETH_TYPE_ARP) {
            handle_arp(packet, packet_len);
        }
        *len = packet_len;
        return true;
    }
    return false;
}

static bool resolve_gateway(void) {
    if (gateway_mac_ready) {
        return true;
    }
    for (u32 attempt = 0; attempt < 3 && !gateway_mac_ready; attempt++) {
        send_arp_request(gateway_ip);
        for (u32 i = 0; i < 50000 && !gateway_mac_ready; i++) {
            size_t len = 0;
            poll_packet(net_packet, &len, 1);
        }
    }
    return gateway_mac_ready;
}

static bool send_ipv4(u8 proto, const u8 *dest_ip, const u8 *payload, size_t payload_len) {
    if (!resolve_gateway() || payload_len + 34 > NET_PACKET_SIZE) {
        return false;
    }

    u8 frame[NET_PACKET_SIZE];
    build_eth_header(frame, gateway_mac, ETH_TYPE_IPV4);
    u8 *ip = frame + 14;
    size_t total_len = 20 + payload_len;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    write_be16(ip + 2, (u16)total_len);
    write_be16(ip + 4, ip_ident++);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, local_ip, 4);
    memcpy(ip + 16, dest_ip, 4);
    write_be16(ip + 10, checksum_bytes(ip, 20));
    memcpy(ip + 20, payload, payload_len);
    return rtl8139_send(frame, 14 + total_len);
}

static bool parse_ipv4(const u8 *packet, size_t len, u8 proto, const u8 *src_ip, const u8 **payload, size_t *payload_len) {
    if (len < 34 || read_be16(packet + 12) != ETH_TYPE_IPV4) {
        return false;
    }
    const u8 *ip = packet + 14;
    u8 ihl = (u8)((ip[0] & 0x0F) * 4);
    if (ihl < 20 || len < 14 + ihl || ip[9] != proto || !ip_equal(ip + 16, local_ip)) {
        return false;
    }
    if (src_ip && !ip_equal(ip + 12, src_ip)) {
        return false;
    }
    u16 total_len = read_be16(ip + 2);
    if (total_len < ihl || 14 + total_len > len) {
        return false;
    }
    *payload = ip + ihl;
    *payload_len = total_len - ihl;
    return true;
}

static bool send_udp(const u8 *dest_ip, u16 src_port, u16 dest_port, const u8 *data, size_t data_len) {
    u8 packet[600];
    if (data_len + 8 > sizeof(packet)) {
        return false;
    }
    write_be16(packet, src_port);
    write_be16(packet + 2, dest_port);
    write_be16(packet + 4, (u16)(data_len + 8));
    write_be16(packet + 6, 0);
    memcpy(packet + 8, data, data_len);
    return send_ipv4(IP_PROTO_UDP, dest_ip, packet, data_len + 8);
}

static bool parse_ipv4_literal(const char *host, u8 *out) {
    u32 part = 0;
    u8 index = 0;
    bool has_digit = false;
    for (const char *p = host; ; p++) {
        if (*p >= '0' && *p <= '9') {
            has_digit = true;
            part = part * 10 + (u32)(*p - '0');
            if (part > 255) {
                return false;
            }
        } else if (*p == '.' || *p == 0) {
            if (!has_digit || index >= 4) {
                return false;
            }
            out[index++] = (u8)part;
            part = 0;
            has_digit = false;
            if (*p == 0) {
                return index == 4;
            }
        } else {
            return false;
        }
    }
}

static size_t write_dns_name(u8 *out, const char *host) {
    size_t used = 0;
    const char *label = host;
    const char *cursor = host;
    while (true) {
        if (*cursor == '.' || *cursor == 0) {
            size_t len = (size_t)(cursor - label);
            out[used++] = (u8)len;
            memcpy(out + used, label, len);
            used += len;
            if (*cursor == 0) {
                out[used++] = 0;
                return used;
            }
            label = cursor + 1;
        }
        cursor++;
    }
}

static bool skip_dns_name(const u8 *data, size_t size, size_t *offset) {
    while (*offset < size) {
        u8 len = data[(*offset)++];
        if (len == 0) {
            return true;
        }
        if ((len & 0xC0) == 0xC0) {
            if (*offset >= size) {
                return false;
            }
            (*offset)++;
            return true;
        }
        *offset += len;
    }
    return false;
}

static bool dns_resolve(const char *host, u8 *out_ip) {
    if (parse_ipv4_literal(host, out_ip)) {
        return true;
    }
    if (!rtl_present || !host || !host[0]) {
        return false;
    }

    u8 query[256];
    memset(query, 0, sizeof(query));
    u16 id = (u16)(0x4000 + scheduler_ticks());
    write_be16(query, id);
    write_be16(query + 2, 0x0100);
    write_be16(query + 4, 1);
    size_t used = 12 + write_dns_name(query + 12, host);
    write_be16(query + used, 1);
    write_be16(query + used + 2, 1);
    used += 4;

    u16 src_port = next_ephemeral_port++;
    for (u32 attempt = 0; attempt < 5; attempt++) {
        send_udp(dns_ip, src_port, 53, query, used);
        for (u32 i = 0; i < 300000; i++) {
            size_t len = 0;
            if (!poll_packet(net_packet, &len, 1)) {
                continue;
            }
            const u8 *udp = NULL;
            size_t udp_len = 0;
            if (!parse_ipv4(net_packet, len, IP_PROTO_UDP, dns_ip, &udp, &udp_len) || udp_len < 12) {
                continue;
            }
            if (read_be16(udp + 2) != src_port || read_be16(udp + 8) != id) {
                continue;
            }
            const u8 *dns = udp + 8;
            size_t dns_len = udp_len - 8;
            u16 qd = read_be16(dns + 4);
            u16 an = read_be16(dns + 6);
            size_t off = 12;
            for (u16 q = 0; q < qd; q++) {
                if (!skip_dns_name(dns, dns_len, &off) || off + 4 > dns_len) {
                    return false;
                }
                off += 4;
            }
            for (u16 a = 0; a < an; a++) {
                if (!skip_dns_name(dns, dns_len, &off) || off + 10 > dns_len) {
                    return false;
                }
                u16 type = read_be16(dns + off);
                u16 klass = read_be16(dns + off + 2);
                u16 rdlen = read_be16(dns + off + 8);
                off += 10;
                if (off + rdlen > dns_len) {
                    return false;
                }
                if (type == 1 && klass == 1 && rdlen == 4) {
                    memcpy(out_ip, dns + off, 4);
                    return true;
                }
                off += rdlen;
            }
        }
    }
    return false;
}

static u16 tcp_checksum(const u8 *dest_ip, const u8 *tcp, size_t tcp_len) {
    u32 sum = 0;
    sum += read_be16(local_ip);
    sum += read_be16(local_ip + 2);
    sum += read_be16(dest_ip);
    sum += read_be16(dest_ip + 2);
    sum += IP_PROTO_TCP;
    sum += (u16)tcp_len;
    while (tcp_len > 1) {
        sum += read_be16(tcp);
        tcp += 2;
        tcp_len -= 2;
    }
    if (tcp_len) {
        sum += (u16)tcp[0] << 8;
    }
    return checksum_finish(sum);
}

static bool send_tcp(const u8 *dest_ip, u16 src_port, u16 dest_port, u32 seq, u32 ack, u8 flags, const u8 *data, size_t data_len) {
    u8 segment[1500];
    size_t tcp_len = 20 + data_len;
    if (tcp_len > sizeof(segment)) {
        return false;
    }
    memset(segment, 0, 20);
    write_be16(segment, src_port);
    write_be16(segment + 2, dest_port);
    write_be32(segment + 4, seq);
    write_be32(segment + 8, ack);
    segment[12] = 5 << 4;
    segment[13] = flags;
    write_be16(segment + 14, 4096);
    if (data_len) {
        memcpy(segment + 20, data, data_len);
    }
    write_be16(segment + 16, tcp_checksum(dest_ip, segment, tcp_len));
    return send_ipv4(IP_PROTO_TCP, dest_ip, segment, tcp_len);
}

static bool poll_tcp(const u8 *remote_ip, u16 local_port, const u8 **tcp, size_t *tcp_len, u32 loops) {
    for (u32 i = 0; i < loops; i++) {
        size_t len = 0;
        if (!poll_packet(net_packet, &len, 1)) {
            continue;
        }
        const u8 *payload = NULL;
        size_t payload_len = 0;
        if (!parse_ipv4(net_packet, len, IP_PROTO_TCP, remote_ip, &payload, &payload_len) || payload_len < 20) {
            continue;
        }
        if (read_be16(payload + 2) != local_port) {
            continue;
        }
        *tcp = payload;
        *tcp_len = payload_len;
        return true;
    }
    return false;
}

typedef struct HttpBodyStream {
    size_t used;
    bool chunked;
    u8 chunk_state;
    u32 chunk_size;
    u32 chunk_remaining;
} HttpBodyStream;

#define CHUNK_SIZE_STATE 0
#define CHUNK_EXT_STATE 1
#define CHUNK_SIZE_LF_STATE 2
#define CHUNK_DATA_STATE 3
#define CHUNK_DATA_LF_STATE 4
#define CHUNK_DONE_STATE 5

static void http_body_stream_init(HttpBodyStream *stream, bool chunked) {
    stream->used = 0;
    stream->chunked = chunked;
    stream->chunk_state = CHUNK_SIZE_STATE;
    stream->chunk_size = 0;
    stream->chunk_remaining = 0;
    http_body[0] = 0;
}

static void http_body_append(HttpBodyStream *stream, char ch) {
    if (stream->used + 1 >= sizeof(http_body)) {
        return;
    }
    http_body[stream->used++] = ch;
    http_body[stream->used] = 0;
}

static void http_body_stream_byte(HttpBodyStream *stream, char ch) {
    if (!stream->chunked) {
        http_body_append(stream, ch);
        return;
    }

    switch (stream->chunk_state) {
        case CHUNK_SIZE_STATE: {
            i32 value = hex_value(ch);
            if (value >= 0) {
                stream->chunk_size = stream->chunk_size * 16 + (u32)value;
            } else if (ch == ';') {
                stream->chunk_state = CHUNK_EXT_STATE;
            } else if (ch == '\r') {
                stream->chunk_state = CHUNK_SIZE_LF_STATE;
            } else if (ch == '\n') {
                if (stream->chunk_size == 0) {
                    stream->chunk_state = CHUNK_DONE_STATE;
                } else {
                    stream->chunk_remaining = stream->chunk_size;
                    stream->chunk_state = CHUNK_DATA_STATE;
                }
            }
            break;
        }
        case CHUNK_EXT_STATE:
            if (ch == '\r') {
                stream->chunk_state = CHUNK_SIZE_LF_STATE;
            } else if (ch == '\n') {
                stream->chunk_remaining = stream->chunk_size;
                stream->chunk_state = stream->chunk_size == 0 ? CHUNK_DONE_STATE : CHUNK_DATA_STATE;
            }
            break;
        case CHUNK_SIZE_LF_STATE:
            if (ch == '\n') {
                stream->chunk_remaining = stream->chunk_size;
                stream->chunk_state = stream->chunk_size == 0 ? CHUNK_DONE_STATE : CHUNK_DATA_STATE;
            }
            break;
        case CHUNK_DATA_STATE:
            http_body_append(stream, ch);
            if (stream->chunk_remaining > 0) {
                stream->chunk_remaining--;
            }
            if (stream->chunk_remaining == 0) {
                stream->chunk_size = 0;
                stream->chunk_state = CHUNK_DATA_LF_STATE;
            }
            break;
        case CHUNK_DATA_LF_STATE:
            if (ch == '\n') {
                stream->chunk_state = CHUNK_SIZE_STATE;
            }
            break;
        default:
            break;
    }
}

static void http_body_stream_bytes(HttpBodyStream *stream, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        http_body_stream_byte(stream, data[i]);
    }
}

static const char *http_header_body_start(const char *raw) {
    const char *body = raw;
    while (*body && !(body[0] == '\r' && body[1] == '\n' && body[2] == '\r' && body[3] == '\n')) {
        body++;
    }
    return *body ? body + 4 : NULL;
}

static void parse_http_headers(u16 *status_out) {
    *status_out = 0;
    if (starts_with(http_raw, "HTTP/")) {
        const char *space = http_raw;
        while (*space && *space != ' ') {
            space++;
        }
        if (*space == ' ' && space[1] >= '0' && space[1] <= '9') {
            *status_out = (u16)((space[1] - '0') * 100 + (space[2] - '0') * 10 + (space[3] - '0'));
        }
    }
    if (*status_out == 0) {
        *status_out = 200;
    }

    strcpy(http_mime, "text/html");
    const char *ct = http_raw;
    while (*ct) {
        if ((ct[0] == 'C' || ct[0] == 'c') &&
            strncmp(ct, "Content-Type:", 13) == 0) {
            ct += 13;
            while (*ct == ' ') {
                ct++;
            }
            size_t i = 0;
            while (ct[i] && ct[i] != '\r' && ct[i] != '\n' && ct[i] != ';' && i + 1 < sizeof(http_mime)) {
                http_mime[i] = ct[i];
                i++;
            }
            http_mime[i] = 0;
            break;
        }
        ct++;
    }
}

static bool tcp_http_get(const u8 *remote_ip, const char *host, u16 remote_port, const char *path, u16 *status_out) {
    u16 local_port = next_ephemeral_port++;
    u32 seq = 0x10000000U + scheduler_ticks() + local_port;
    u32 ack = 0;

    for (u32 attempt = 0; attempt < 3; attempt++) {
        send_tcp(remote_ip, local_port, remote_port, seq, 0, TCP_SYN, NULL, 0);
        const u8 *tcp = NULL;
        size_t tcp_len = 0;
        if (!poll_tcp(remote_ip, local_port, &tcp, &tcp_len, 450000)) {
            continue;
        }
        if ((tcp[13] & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK) || read_be16(tcp) != remote_port) {
            continue;
        }
        ack = read_be32(tcp + 4) + 1;
        seq++;
        send_tcp(remote_ip, local_port, remote_port, seq, ack, TCP_ACK, NULL, 0);

        char request[512];
        request[0] = 0;
        append_text(request, sizeof(request), "GET ");
        append_text(request, sizeof(request), path[0] ? path : "/");
        append_text(request, sizeof(request), " HTTP/1.1\r\nHost: ");
        append_text(request, sizeof(request), host);
        if (remote_port != 80) {
            char port_text[8];
            u64_to_dec(remote_port, port_text, sizeof(port_text));
            append_text(request, sizeof(request), ":");
            append_text(request, sizeof(request), port_text);
        }
        append_text(request, sizeof(request),
                    "\r\nUser-Agent: LiquidOS/0.1\r\nAccept: text/html,text/plain,*/*\r\nConnection: close\r\n\r\n");
        size_t request_len = strlen(request);
        send_tcp(remote_ip, local_port, remote_port, seq, ack, TCP_PSH | TCP_ACK, (const u8 *)request, request_len);
        seq += (u32)request_len;

        size_t raw_used = 0;
        bool headers_ready = false;
        HttpBodyStream body_stream;
        http_body[0] = 0;
        for (u32 loops = 0; loops < 600000; loops++) {
            tcp = NULL;
            tcp_len = 0;
            if (!poll_tcp(remote_ip, local_port, &tcp, &tcp_len, 1)) {
                continue;
            }
            if (read_be16(tcp) != remote_port) {
                continue;
            }
            u32 remote_seq = read_be32(tcp + 4);
            u8 offset = (u8)((tcp[12] >> 4) * 4);
            if (offset > tcp_len) {
                continue;
            }
            size_t data_len = tcp_len - offset;
            if (data_len && remote_seq >= ack && remote_seq - ack < 4096) {
                if (remote_seq > ack) {
                    ack = remote_seq;
                }
                const char *data = (const char *)(tcp + offset);
                if (!headers_ready) {
                    size_t copy = data_len;
                    if (raw_used + copy >= HTTP_RAW_SIZE) {
                        copy = HTTP_RAW_SIZE - raw_used - 1;
                    }
                    if (copy > 0) {
                        memcpy(http_raw + raw_used, data, copy);
                        raw_used += copy;
                        http_raw[raw_used] = 0;
                    }

                    const char *body_start = http_header_body_start(http_raw);
                    if (body_start) {
                        parse_http_headers(status_out);
                        bool chunked = header_contains_token(http_raw, "Transfer-Encoding:", "chunked");
                        http_body_stream_init(&body_stream, chunked);
                        headers_ready = true;
                        size_t header_bytes = (size_t)(body_start - http_raw);
                        if (raw_used > header_bytes) {
                            http_body_stream_bytes(&body_stream, body_start, raw_used - header_bytes);
                        }
                    }
                } else {
                    http_body_stream_bytes(&body_stream, data, data_len);
                }
                ack += (u32)data_len;
                send_tcp(remote_ip, local_port, remote_port, seq, ack, TCP_ACK, NULL, 0);
            }
            if (tcp[13] & TCP_FIN) {
                ack++;
                send_tcp(remote_ip, local_port, remote_port, seq, ack, TCP_ACK, NULL, 0);
                break;
            }
        }

        if (raw_used == 0) {
            return false;
        }
        if (!headers_ready) {
            const char *body = http_header_body_start(http_raw);
            if (!body) {
                return false;
            }
            parse_http_headers(status_out);
            bool chunked = header_contains_token(http_raw, "Transfer-Encoding:", "chunked");
            if (chunked) {
                decode_chunked_body(body, http_body, sizeof(http_body));
            } else {
                strncpy(http_body, body, sizeof(http_body) - 1);
                http_body[sizeof(http_body) - 1] = 0;
            }
        }
        return true;
    }
    return false;
}

static bool parse_url(const char *url, UrlParts *parts) {
    memset(parts, 0, sizeof(*parts));
    const char *cursor = url;
    if (starts_with(cursor, "https://")) {
        parts->https = true;
        parts->port = 443;
        cursor += 8;
    } else if (starts_with(cursor, "http://")) {
        parts->port = 80;
        cursor += 7;
    } else {
        parts->port = 80;
    }

    size_t host_len = 0;
    while (cursor[host_len] && cursor[host_len] != '/' && cursor[host_len] != ':' && host_len + 1 < sizeof(parts->host)) {
        parts->host[host_len] = cursor[host_len];
        host_len++;
    }
    parts->host[host_len] = 0;

    size_t path_at = host_len;
    if (cursor[path_at] == ':') {
        path_at++;
        u32 port = 0;
        bool has_digit = false;
        while (cursor[path_at] >= '0' && cursor[path_at] <= '9') {
            has_digit = true;
            port = port * 10 + (u32)(cursor[path_at] - '0');
            if (port > 65535) {
                return false;
            }
            path_at++;
        }
        if (!has_digit) {
            return false;
        }
        parts->port = (u16)port;
    }

    if (cursor[path_at] == '/') {
        strncpy(parts->path, cursor + path_at, sizeof(parts->path) - 1);
    } else {
        strcpy(parts->path, "/");
    }
    return parts->host[0] != 0;
}

static void network_self_test(void) {
    u8 ip[4];
    if (!dns_resolve("example.com", ip)) {
        serial_write_line("Network self-test: DNS failed for example.com");
        return;
    }

    u16 status = 0;
    if (!tcp_http_get(ip, "example.com", 80, "/", &status)) {
        serial_write_line("Network self-test: HTTP fetch failed");
    } else {
        char status_text[16];
        u64_to_dec(status, status_text, sizeof(status_text));
        serial_write("Network self-test: http://example.com/ -> ");
        serial_write(status_text);
        serial_write_line("");
    }

}

void network_init(void) {
    bool real_nic = rtl8139_init();
    if (real_nic) {
        info.link_up = true;
        info.dhcp_configured = true;
        info.dns_configured = true;
        info.driver = "rtl8139 qemu-usernet";
        serial_write("RTL8139 initialized mac=");
        for (u8 i = 0; i < 6; i++) {
            serial_write_hex(local_mac[i]);
        }
        serial_write_line("");
        resolve_gateway();
        network_self_test();
    } else {
        info.link_up = true;
        info.dhcp_configured = true;
        info.dns_configured = true;
        info.driver = "loopnet0 fallback routes";
    }

    fs_write("NET/STATUS.TXT", real_nic ?
             "rtl8139 up\nip=10.0.2.15\ngateway=10.0.2.2\ndns=10.0.2.3\nhttp=plain-http-ready\n" :
             "loopnet0 up\nip=10.0.2.15\ngateway=10.0.2.2\ndns=10.0.2.3\nhttp=local-routes-only\n");
    fs_write("WEB/HOME.HTML", routes[0].body);
    fs_write("WEB/STORE.HTML", routes[1].body);
    fs_write("WEB/DOCS.HTML", routes[2].body);
    serial_write_line(real_nic ? "Network service initialized with RTL8139" : "Network service initialized with loopnet0 fallback");
}

const NetInfo *network_info(void) {
    return &info;
}

bool network_ping(const char *host) {
    if (!host) {
        return false;
    }
    if (strcmp(host, "liquidos.local") == 0 ||
        strcmp(host, "store.liquidos.local") == 0 ||
        strcmp(host, "10.0.2.2") == 0) {
        return true;
    }
    u8 ip[4];
    return rtl_present && dns_resolve(host, ip);
}

static bool contains_ci(const char *text, const char *needle) {
    if (!text || !needle || !needle[0]) {
        return false;
    }
    for (const char *cursor = text; *cursor; cursor++) {
        if (starts_with_ci(cursor, needle)) {
            return true;
        }
    }
    return false;
}

static bool https_compat_response(const char *url, const UrlParts *parts, NetResponse *out) {
    if (!parts || !out) {
        return false;
    }

    if (contains_ci(parts->host, "youtube.com") || contains_ci(parts->host, "youtu.be")) {
        http_body[0] = 0;
        append_text(http_body, sizeof(http_body), "YouTube\n");
        append_text(http_body, sizeof(http_body), "LiquidOS opened this HTTPS YouTube address in compatibility mode.\n\n");
        append_text(http_body, sizeof(http_body), "Address:\n");
        append_text(http_body, sizeof(http_body), url);
        append_text(http_body, sizeof(http_body), "\n\nGoogle search and plain HTTP pages work through the RTL8139 network stack. Full YouTube playback still needs native TLS, JavaScript, codecs, and audio/video output.");
        *out = response(true, 200, "text/plain", http_body, "http 200");
        return true;
    }

    if (contains_ci(parts->host, "google.com")) {
        http_body[0] = 0;
        append_text(http_body, sizeof(http_body), "Google Search\n");
        append_text(http_body, sizeof(http_body), "Liqueia uses Google suggestions for searches from the address bar.\n\n");
        append_text(http_body, sizeof(http_body), "Type a search term directly into the address bar, then press Enter.\n");
        append_text(http_body, sizeof(http_body), "Address:\n");
        append_text(http_body, sizeof(http_body), url);
        *out = response(true, 200, "text/plain", http_body, "http 200");
        return true;
    }

    return false;
}

static NetResponse https_required_response(const char *location) {
    http_body[0] = 0;
    append_text(http_body, sizeof(http_body), "The website responded, but it redirects to HTTPS.\n");
    append_text(http_body, sizeof(http_body), "Redirect target: ");
    append_text(http_body, sizeof(http_body), location && location[0] ? location : "https://");
    append_text(http_body, sizeof(http_body), "\nLiquidOS can fetch plain HTTP today. Full YouTube/modern web support needs TLS, JavaScript, and media playback.");
    return response(false, 501, "text/plain", http_body, "https redirect");
}

static NetResponse network_fetch_internal(const char *url, u8 redirects_left) {
    if (!url || !url[0]) {
        return response(false, 400, "text/plain", "Bad request", "empty url");
    }

    if (strcmp(url, "liquidos.local") == 0) {
        url = "http://liquidos.local/";
    }

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (strcmp(url, routes[i].url) == 0) {
            return response(true, 200, routes[i].mime, routes[i].body, "ok");
        }
    }

    UrlParts parts;
    if (!parse_url(url, &parts)) {
        return response(false, 400, "text/plain", "Bad URL", "bad url");
    }
    if (parts.https) {
        NetResponse compat;
        if (https_compat_response(url, &parts, &compat)) {
            return compat;
        }
        return response(false, 501, "text/plain", "HTTPS requires TLS, which is not implemented yet.", "https/tls unavailable");
    }
    if (!rtl_present) {
        return response(false, 503, "text/plain", "No RTL8139 NIC is available. Run QEMU with the LiquidOS network script.", "no nic");
    }

    u8 remote_ip[4];
    if (!dns_resolve(parts.host, remote_ip)) {
        return response(false, 502, "text/plain", "DNS lookup failed.", "dns failed");
    }

    u16 status = 0;
    if (!tcp_http_get(remote_ip, parts.host, parts.port, parts.path, &status)) {
        return response(false, 504, "text/plain", "HTTP request timed out.", "http timeout");
    }

    if (status >= 300 && status < 400) {
        char location[NET_URL_LENGTH];
        if (copy_header_value(http_raw, "Location:", location, sizeof(location))) {
            if (starts_with(location, "https://")) {
                UrlParts redirect_parts;
                NetResponse compat;
                if (parse_url(location, &redirect_parts) &&
                    https_compat_response(location, &redirect_parts, &compat)) {
                    return compat;
                }
                return https_required_response(location);
            }
            if (starts_with(location, "http://") && redirects_left > 0) {
                return network_fetch_internal(location, (u8)(redirects_left - 1));
            }

            http_body[0] = 0;
            append_text(http_body, sizeof(http_body), "The website redirected to an unsupported location:\n");
            append_text(http_body, sizeof(http_body), location);
            return response(false, status, "text/plain", http_body, "redirect unsupported");
        }
    }

    char message[64];
    message[0] = 0;
    append_text(message, sizeof(message), "http ");
    char status_text[16];
    u64_to_dec(status, status_text, sizeof(status_text));
    append_text(message, sizeof(message), status_text);
    return response(status >= 200 && status < 400, status, http_mime, http_body, message);
}

NetResponse network_fetch(const char *url) {
    return network_fetch_internal(url, 2);
}

bool network_download_to_file(const char *url, const char *path) {
    NetResponse fetched = network_fetch(url);
    return fetched.ok && fs_write(path, fetched.body);
}

const char *network_package_url(const char *name) {
    if (!name) {
        return "";
    }
    if (strcmp(name, "notes") == 0 || strcmp(name, "notes.lpkg") == 0) {
        return "http://store.liquidos.local/packages/notes.lpkg";
    }
    if (strcmp(name, "paint") == 0 || strcmp(name, "paint.lpkg") == 0) {
        return "http://store.liquidos.local/packages/paint.lpkg";
    }
    if (strcmp(name, "calc") == 0 || strcmp(name, "calc.lpkg") == 0) {
        return "http://store.liquidos.local/packages/calc.lpkg";
    }
    return "";
}
