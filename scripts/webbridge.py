#!/usr/bin/env python3
import argparse
import socket
import threading
import urllib.parse
import urllib.request


MAX_FETCH_BYTES = 2 * 1024 * 1024
MAX_REPLY_BYTES = 16 * 1024


def safe_content_type(content_type):
    content_type = (content_type or "text/plain").split(";", 1)[0].strip().lower()
    if content_type in {
        "text/html",
        "text/plain",
        "text/css",
        "application/xhtml+xml",
        "application/json",
    }:
        return "text/html" if content_type == "application/xhtml+xml" else content_type
    return "text/plain"


def http_response(status, reason, payload, content_type="text/plain"):
    headers = (
        f"HTTP/1.1 {status} {reason}\r\n"
        f"Content-Type: {content_type}; charset=utf-8\r\n"
        f"Content-Length: {len(payload)}\r\n"
        "Connection: close\r\n"
        "\r\n"
    )
    return headers.encode("ascii") + payload


def fetch_payload(target):
    try:
        request = urllib.request.Request(
            target,
            headers={
                "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X) AppleWebKit/537.36 Chrome/124 Safari/537.36",
                "Accept": "text/html,text/plain,*/*",
                "Accept-Language": "en-GB,en;q=0.9",
            },
        )
        with urllib.request.urlopen(request, timeout=18) as response:
            body = response.read(MAX_FETCH_BYTES)
            content_type = safe_content_type(response.headers.get("content-type", "text/plain"))
            return body[:MAX_REPLY_BYTES], content_type
    except Exception as exc:
        return f"LiquidOS WebBridge could not load:\n{target}\n\n{exc}".encode("utf-8", "replace"), "text/plain"


def handle_request(data):
    request_line = data.split(b"\r\n", 1)[0].decode("iso-8859-1", "replace")
    parts = request_line.split(" ")
    if len(parts) < 2 or parts[0] != "GET":
        return http_response(405, "Method Not Allowed", b"Use GET /fetch?url=https://example.com/")

    parsed = urllib.parse.urlparse(parts[1])
    if parsed.path != "/fetch":
        return http_response(404, "Not Found", b"Use /fetch?url=https://example.com/")

    target = urllib.parse.parse_qs(parsed.query).get("url", [""])[0]
    if not target.startswith(("https://", "http://")):
        return http_response(400, "Bad Request", b"Missing absolute url")

    payload, content_type = fetch_payload(target)
    return http_response(200, "OK", payload, content_type)


def read_http_request(read):
    data = b""
    while b"\r\n\r\n" not in data and len(data) < 8192:
        chunk = read(4096)
        if not chunk:
            break
        data += chunk
    return data


def handle_client(conn):
    try:
        conn.settimeout(90)
        data = read_http_request(conn.recv)
        conn.sendall(handle_request(data))
    finally:
        conn.close()


def serve(host, port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(16)
        print(f"LiquidOS WebBridge listening on {host}:{port}", flush=True)
        while True:
            conn, _addr = server.accept()
            threading.Thread(target=handle_client, args=(conn,), daemon=True).start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8087, type=int)
    args = parser.parse_args()
    serve(args.host, args.port)


if __name__ == "__main__":
    main()
