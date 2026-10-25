#!/usr/bin/env python3
"""Mira Camera beta: one-shot JPEG snapshot -> 24-bit BMP at screen size.

Start on a trusted LAN host: python3 -m pip install pillow
  CAMERA_SNAPSHOT_URL='http://camera.local/snapshot.jpg' python3 tools/mira_camera_proxy.py

No camera URL? A generated test image is served, to validate the ESP32 viewer.
NOT an internet-facing server: pictures are served without authentication.
"""
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import os
from urllib.parse import parse_qs, urlsplit
from urllib.request import Request, urlopen

from PIL import Image, ImageDraw, ImageOps

SNAPSHOT_URL = os.getenv("CAMERA_SNAPSHOT_URL", "").strip()
USERNAME = os.getenv("CAMERA_USER", "")
PASSWORD = os.getenv("CAMERA_PASSWORD", "")
HOST = os.getenv("MIRA_CAMERA_HOST", "0.0.0.0")
PORT = int(os.getenv("MIRA_CAMERA_PORT", "8098"))
MAX_SOURCE = 6 * 1024 * 1024


def get_source_image():
    if not SNAPSHOT_URL:
        image = Image.new("RGB", (800, 480), (20, 35, 50))
        d = ImageDraw.Draw(image)
        d.rectangle((30, 30, 770, 450), outline=(65, 180, 235), width=6)
        d.text((70, 130), "MIRA CAMERA - IMAGE DE TEST", fill=(255, 255, 255))
        d.text((70, 190), "La connexion BMP fonctionne !", fill=(100, 235, 180))
        return image
    if urlsplit(SNAPSHOT_URL).scheme not in ("http", "https"):
        raise ValueError("CAMERA_SNAPSHOT_URL must be HTTP(S)")
    headers = {"User-Agent": "MiraCameraBeta/1", "Accept": "image/jpeg,image/png,image/*"}
    if USERNAME:
        credentials = base64.b64encode((USERNAME + ":" + PASSWORD).encode()).decode("ascii")
        headers["Authorization"] = "Basic " + credentials
    request = Request(SNAPSHOT_URL, headers=headers)
    with urlopen(request, timeout=5) as response:
        if int(response.headers.get("Content-Length", "0")) > MAX_SOURCE:
            raise ValueError("Snapshot exceeds size limit")
        raw = response.read(MAX_SOURCE + 1)
    if len(raw) > MAX_SOURCE:
        raise ValueError("Snapshot exceeds size limit")
    with Image.open(BytesIO(raw)) as loaded:
        return ImageOps.exif_transpose(loaded).convert("RGB")


def render_bmp(width: int, height: int) -> bytes:
    source = get_source_image()
    frame = Image.new("RGB", (width, height), (0, 0, 0))
    image = ImageOps.contain(source, (width, height), Image.Resampling.BILINEAR)
    frame.paste(image, ((width - image.width) // 2, (height - image.height) // 2))
    out = BytesIO()
    frame.save(out, format="BMP")  # Windows BMP, uncompressed BGR24, bottom-up.
    return out.getvalue()


class CameraHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlsplit(self.path)
        if path.path != "/snapshot.bmp":
            self.send_error(404)
            return
        try:
            qs = parse_qs(path.query, keep_blank_values=False)
            width = int(qs.get("w", ["320"])[0])
            height = int(qs.get("h", ["240"])[0])
            if not 40 <= width <= 800 or not 40 <= height <= 480:
                raise ValueError("dimensions out of range")
            bmp = render_bmp(width, height)
            if bmp[:2] != b"BM" or int.from_bytes(bmp[28:30], "little") != 24:
                raise ValueError("not a 24-bit BMP")
        except (ValueError, OSError, Exception) as exc:
            self.send_error(502, "Camera snapshot unavailable: " + str(exc)[:140])
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/bmp")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(bmp)))
        self.end_headers()
        self.wfile.write(bmp)


if __name__ == "__main__":
    print("Mira Camera beta on http://%s:%d/snapshot.bmp" % (HOST, PORT))
    print("Camera source: %s" % ("configured" if SNAPSHOT_URL else "DEMO"))
    print("SECURITY: LAN only, no authentication on BMP output")
    ThreadingHTTPServer((HOST, PORT), CameraHandler).serve_forever()
