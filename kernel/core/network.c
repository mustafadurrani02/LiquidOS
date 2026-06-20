#include <liquidos/fs.h>
#include <liquidos/lib.h>
#include <liquidos/network.h>
#include <liquidos/serial.h>

typedef struct NetRoute {
    const char *url;
    const char *mime;
    const char *body;
} NetRoute;

static NetInfo info = {
    true,
    true,
    true,
    "loopnet0 virtual HTTP transport",
    "10.0.2.15",
    "10.0.2.2",
    "10.0.2.3",
};

static const NetRoute routes[] = {
    {
        "http://liquidos.local/",
        "text/html",
        "LiquidOS Network Home\n"
        "Status: online through loopnet0\n"
        "Try http://liquidos.local/store or http://liquidos.local/docs\n"
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
        "The OS now has a network service API, DNS/HTTP-style fetches, and download-to-file support.\n"
        "A real e1000 or virtio-net driver can replace loopnet0 later.\n"
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

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
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

void network_init(void) {
    fs_write("NET/STATUS.TXT", "loopnet0 up\nip=10.0.2.15\ngateway=10.0.2.2\ndns=10.0.2.3\nhttp=ready\n");
    fs_write("WEB/HOME.HTML", routes[0].body);
    fs_write("WEB/STORE.HTML", routes[1].body);
    fs_write("WEB/DOCS.HTML", routes[2].body);
    serial_write_line("Network service initialized");
}

const NetInfo *network_info(void) {
    return &info;
}

bool network_ping(const char *host) {
    return host &&
           (strcmp(host, "liquidos.local") == 0 ||
            strcmp(host, "store.liquidos.local") == 0 ||
            strcmp(host, "10.0.2.2") == 0);
}

NetResponse network_fetch(const char *url) {
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

    if (starts_with(url, "http://")) {
        return response(false, 404, "text/plain", "404 Not Found", "route not found");
    }

    return response(false, 501, "text/plain", "Only HTTP routes are available in this build.", "unsupported scheme");
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
