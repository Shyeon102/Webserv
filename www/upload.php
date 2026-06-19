#!/usr/bin/env python3
import html
import os
import re
import sys
import time
import urllib.parse


UPLOAD_DIR = os.path.join(os.getcwd(), "uploads")


def response(body, status="200 OK"):
    sys.stdout.write("Status: " + status + "\r\n")
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write("\r\n")
    sys.stdout.write(body)


def page(title, content):
    return """<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>{title}</title>
  <style>
    body {{ font-family: system-ui, sans-serif; margin: 32px; line-height: 1.45; }}
    table {{ border-collapse: collapse; min-width: 520px; }}
    th, td {{ border: 1px solid #ccc; padding: 8px 10px; text-align: left; }}
    code {{ background: #f2f2f2; padding: 2px 4px; }}
  </style>
</head>
<body>
  <h1>{title}</h1>
  {content}
</body>
</html>
""".format(title=html.escape(title), content=content)


def parse_header_params(value):
    params = {}
    parts = [part.strip() for part in value.split(";")]
    params[""] = parts[0].lower() if parts else ""
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, raw = part.split("=", 1)
        raw = raw.strip()
        if len(raw) >= 2 and raw[0] == '"' and raw[-1] == '"':
            raw = raw[1:-1]
        params[key.strip().lower()] = raw
    return params


def safe_filename(name):
    base = os.path.basename(name)
    cleaned = re.sub(r"[^A-Za-z0-9._-]", "_", base)
    return cleaned or "upload.bin"


def read_body():
    try:
        length = int(os.environ.get("CONTENT_LENGTH", "0"))
    except ValueError:
        length = 0
    if length <= 0:
        return b""
    return sys.stdin.buffer.read(length)


def parse_multipart(body, content_type):
    params = parse_header_params(content_type)
    boundary = params.get("boundary", "")
    if not boundary:
        return [], "missing multipart boundary"

    files = []
    fields = []
    marker = ("--" + boundary).encode("latin-1")
    for raw_part in body.split(marker):
        if not raw_part or raw_part in (b"--", b"--\r\n", b"\r\n"):
            continue
        if raw_part.startswith(b"\r\n"):
            raw_part = raw_part[2:]
        if raw_part.endswith(b"--"):
            raw_part = raw_part[:-2]
        if raw_part.endswith(b"\r\n"):
            raw_part = raw_part[:-2]

        if b"\r\n\r\n" in raw_part:
            raw_headers, data = raw_part.split(b"\r\n\r\n", 1)
        elif b"\n\n" in raw_part:
            raw_headers, data = raw_part.split(b"\n\n", 1)
        else:
            continue

        headers = {}
        for line in raw_headers.decode("latin-1", "replace").splitlines():
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            headers[key.strip().lower()] = value.strip()

        disposition = parse_header_params(headers.get("content-disposition", ""))
        field_name = disposition.get("name", "")
        filename = disposition.get("filename")
        if filename is None or filename == "":
            fields.append((field_name, data.decode("utf-8", "replace")))
            continue

        os.makedirs(UPLOAD_DIR, exist_ok=True)
        stored_name = "cgi-{0}-{1}".format(int(time.time()), safe_filename(filename))
        stored_path = os.path.join(UPLOAD_DIR, stored_name)
        with open(stored_path, "wb") as fp:
            fp.write(data)
        files.append((field_name, filename, stored_name, len(data)))

    return (files, fields), None


def handle_post():
    method = os.environ.get("REQUEST_METHOD", "GET")
    content_type = os.environ.get("CONTENT_TYPE", "")
    body = read_body()

    rows = [
        ("method", method),
        ("content-type", content_type),
        ("content-length", str(len(body))),
    ]

    if content_type.startswith("multipart/form-data"):
        result, error = parse_multipart(body, content_type)
        if error:
            rows.append(("multipart error", error))
        else:
            files, fields = result
            for name, value in fields:
                rows.append(("field " + name, value))
            for name, original, stored, size in files:
                rows.append(("file field", name))
                rows.append(("original name", original))
                rows.append(("stored as", "/uploads/" + stored))
                rows.append(("size", str(size)))
            if not files and not fields:
                rows.append(("multipart", "no parts found"))
    elif content_type.startswith("application/x-www-form-urlencoded"):
        parsed = urllib.parse.parse_qs(body.decode("utf-8", "replace"))
        for key, values in parsed.items():
            rows.append(("field " + key, ", ".join(values)))
    else:
        preview = body[:200].decode("utf-8", "replace")
        rows.append(("raw body preview", preview))

    table = "<table><tbody>"
    for key, value in rows:
        table += "<tr><th>{}</th><td>{}</td></tr>".format(
            html.escape(key), html.escape(value)
        )
    table += "</tbody></table>"
    table += '<p><a href="/">Back to index</a></p>'
    response(page("CGI upload.php result", table))


def handle_get():
    content = """
<form method="post" enctype="multipart/form-data" action="/upload.php">
  <p><input type="file" name="Yofile"></p>
  <p><button type="submit">Upload with CGI</button></p>
</form>
<p>Saved files go to <code>/uploads/</code>.</p>
"""
    response(page("CGI upload.php", content))


if os.environ.get("REQUEST_METHOD", "GET") == "POST":
    handle_post()
else:
    handle_get()
