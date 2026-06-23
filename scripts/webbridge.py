#!/usr/bin/env python3
import argparse
import html
import json
import re
import socket
import threading
import urllib.parse
import urllib.request


MAX_FETCH_BYTES = 2 * 1024 * 1024
MAX_REPLY_BYTES = 28 * 1024


def html_title(text):
    match = re.search(r"(?is)<title[^>]*>(.*?)</title>", text)
    if not match:
        return ""
    return html.unescape(re.sub(r"(?is)<[^>]+>", " ", match.group(1))).strip()


def clean_line(text):
    return " ".join(html.unescape(text).split())


def html_to_text(body, content_type):
    text = body.decode("utf-8", "replace")
    if "html" not in content_type.lower():
        return text[:MAX_REPLY_BYTES]

    title = html_title(text)
    text = re.sub(r"(?is)<script.*?</script>", " ", text)
    text = re.sub(r"(?is)<style.*?</style>", " ", text)
    text = re.sub(r"(?i)<br\s*/?>", "\n", text)
    text = re.sub(r"(?i)</(p|div|li|h[1-6]|tr)>", "\n", text)
    text = re.sub(r"(?is)<[^>]+>", " ", text)
    lines = []
    seen = set()
    for raw in text.splitlines():
        line = clean_line(raw)
        if len(line) < 3 or line in seen:
            continue
        seen.add(line)
        lines.append(line)
        if len("\n".join(lines)) >= MAX_REPLY_BYTES:
            break
    if title:
        lines.insert(0, title)
    return "\n".join(lines)[:MAX_REPLY_BYTES]


def youtube_text(url, body):
    text = body.decode("utf-8", "replace")
    title = html_title(text) or "YouTube"
    lines = [
        "YouTube",
        f"Fetched live from {url}",
        f"Page title: {title}",
        "",
        "Real HTTPS/TLS fetch completed through LiquidOS WebBridge.",
    ]

    candidates = []
    patterns = [
        r'"videoRenderer"\s*:\s*\{.*?"title"\s*:\s*\{"runs"\s*:\s*\[\{"text"\s*:\s*"([^"]+)"',
        r'"reelItemRenderer"\s*:\s*\{.*?"headline"\s*:\s*\{"simpleText"\s*:\s*"([^"]+)"',
        r'"title"\s*:\s*\{"runs"\s*:\s*\[\{"text"\s*:\s*"([^"]{4,90})"',
        r'"simpleText"\s*:\s*"([^"]{4,90})"',
    ]
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            try:
                value = json.loads(f'"{match.group(1)}"')
            except Exception:
                value = match.group(1)
            value = clean_line(value)
            if not value or value in candidates:
                continue
            lowered = value.lower()
            if any(skip in lowered for skip in ["youtube", "cookies", "sign in", "google llc", "privacy", "terms"]):
                continue
            candidates.append(value)
            if len(candidates) >= 10:
                break
        if len(candidates) >= 6:
            break

    if candidates:
        lines += ["", "Live page items:"]
        lines += [f"- {item}" for item in candidates[:10]]
    else:
        lines += [
            "",
            "YouTube returned its app shell. Native video playback still needs JavaScript, DOM/CSS, codecs, and audio output.",
        ]
    return "\n".join(lines)


def google_text(url, body):
    text = html_to_text(body, "text/html")
    return f"Google\nFetched live from {url}\n\n{text}"


def bridge_page(url, status, content_type, body):
    host = urllib.parse.urlparse(url).netloc.lower()
    if "youtube.com" in host or "youtu.be" in host:
        rendered = youtube_text(url, body)
    elif "google." in host:
        rendered = google_text(url, body)
    else:
        rendered = f"Fetched live from {url}\nHTTP {status}\n\n{html_to_text(body, content_type)}"
    return rendered.encode("utf-8", "replace")[:MAX_REPLY_BYTES]


def http_response(status, reason, payload):
    headers = (
        f"HTTP/1.1 {status} {reason}\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
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
            content_type = response.headers.get("content-type", "text/plain")
            return bridge_page(response.geturl(), response.status, content_type, body)
    except Exception as exc:
        return f"LiquidOS WebBridge could not load:\n{target}\n\n{exc}".encode("utf-8", "replace")


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

    return http_response(200, "OK", fetch_payload(target))


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
