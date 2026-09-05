#!/usr/bin/env python3
"""
Robust ElegantOTA v3 HTTP upload for PlatformIO.
Uses stdlib only — no external dependencies.

Flow:  GET /ota/start?mode=firmware  →  POST /ota/upload (multipart)

PlatformIO invokes this as:
    python scripts/http_ota.py <host_ip> <firmware.bin>
"""
import sys
import base64
import http.client
import time
import os
import re
import hashlib
from pathlib import Path


def load_secret(name):
    """Read a #define from include/secrets.h, falling back to the environment."""
    header = Path(__file__).resolve().parent.parent / "include" / "secrets.h"
    if header.exists():
        m = re.search(r'#define\s+%s\s+"([^"]*)"' %
                      name, header.read_text(encoding="utf-8"))
        if m:
            return m.group(1)
    if name in os.environ:
        return os.environ[name]
    print("[OTA] %s not found - create include/secrets.h from secrets.example.h" % name)
    sys.exit(1)


if len(sys.argv) < 3:
    print("Usage: http_ota.py <host> <firmware.bin>")
    sys.exit(1)

host = sys.argv[1]
bin_path = sys.argv[2]
user = load_secret("OTA_HTTP_USER")
password = load_secret("OTA_HTTP_PASS")

# ── Load firmware ─────────────────────────────────────────────
size = os.path.getsize(bin_path)
print("[OTA] %s  %d KB  ->  http://%s" %
      (Path(bin_path).name, size // 1024, host))

with open(bin_path, "rb") as f:
    firmware = f.read()

md5 = hashlib.md5(firmware).hexdigest()
auth = base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
base_headers = {"Authorization": "Basic %s" % auth}


def request(method, path, extra_headers=None, body=None):
    headers = dict(base_headers)
    if extra_headers:
        headers.update(extra_headers)
    conn = http.client.HTTPConnection(host, port=80, timeout=120)
    conn.request(method, path, body=body, headers=headers)
    try:
        resp = conn.getresponse()
        data = resp.read(512)
        conn.close()
        return resp.status, data.decode(errors="replace")
    except (http.client.RemoteDisconnected, ConnectionResetError, BrokenPipeError):
        return 0, "connection closed (reboot)"
    except Exception as e:
        return -1, str(e)


# ── Step 1: initialise OTA ────────────────────────────────────
print("[OTA] Step 1: GET /ota/start ...")
status, body_txt = request("GET", "/ota/start?mode=firmware&hash=%s" % md5)
print("[OTA]   -> HTTP %d  %s" % (status, body_txt[:80]))
if status not in (200, 0):
    print("[OTA] FAILED at /ota/start — device returned %d" % status)
    sys.exit(1)

# ── Step 2: upload binary via multipart POST ──────────────────
boundary = b"PIOotaBoundary0001"
CRLF = b"\r\n"
body_bytes = b"--" + boundary + CRLF
body_bytes += b'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"' + CRLF
body_bytes += b"Content-Type: application/octet-stream" + CRLF + CRLF
body_bytes += firmware + CRLF
body_bytes += b"--" + boundary + b"--" + CRLF

upload_headers = {
    "Content-Type": "multipart/form-data; boundary=%s" % boundary.decode(),
    "Content-Length": str(len(body_bytes)),
    "Connection": "close",
}

print("[OTA] Step 2: POST /ota/upload (%d KB) ..." % (len(body_bytes) // 1024))
status, body_txt = request("POST", "/ota/upload",
                           extra_headers=upload_headers, body=body_bytes)
print("[OTA]   -> HTTP %d  %s" % (status, body_txt[:80]))

# ── Wait for reboot ───────────────────────────────────────────
print("[OTA] Waiting for device", end="", flush=True)
for i in range(40):
    time.sleep(2)
    try:
        conn = http.client.HTTPConnection(host, port=80, timeout=3)
        conn.request("GET", "/")
        r = conn.getresponse()
        r.read()
        conn.close()
        if r.status == 200:
            print("\n[OTA] Device online after %ds -- upload successful!" %
                  ((i + 1) * 2))
            sys.exit(0)
    except Exception:
        print(".", end="", flush=True)

print("\n[OTA] FAILED -- device did not respond in 80s")
sys.exit(1)
