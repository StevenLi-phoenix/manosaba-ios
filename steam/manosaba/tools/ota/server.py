#!/usr/bin/env python3
"""Tailnet-only install page for the iPad: UDID capture (Profile Service) + OTA install.

Run behind `tailscale serve --https=8443 http://127.0.0.1:8765`.
  GET  /                  landing page (Chinese), shows captured UDID and the install button
  GET  /udid.mobileconfig Profile Service payload asking the device for its UDID
  POST /udid              device callback (CMS-signed plist) -> logs/ipad-udid.json
  GET  /manifest.plist    itms-services manifest (when build/ota/manosaba.ipa exists)
  GET  /manosaba.ipa      the IPA, with HTTP Range support
"""
from __future__ import annotations

import html
import json
import logging
import os
import plistlib
import re
import sys
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# The HTTPS origin the device reaches, e.g. the `tailscale serve --https=8443` URL of this Mac.
BASE = os.environ.get("OTA_BASE", "https://your-mac.your-tailnet.ts.net:8443")
IPA = ROOT / "build/ota/manosaba.ipa"
UDID_FILE = ROOT / "logs/ipad-udid.json"
BUNDLE_ID = os.environ.get("MANOSABA_BUNDLE_ID", "com.example.manosaba")

log = logging.getLogger("ota")

PAGE = """<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>魔女裁判 · iPad 安装</title>
<style>
:root{{--bg:#120e1a;--card:#1d1828;--ink:#f2ecff;--mute:#a99cc4;--accent:#b48cff}}
body{{margin:0;background:var(--bg);color:var(--ink);font:17px/1.6 -apple-system,"PingFang SC",sans-serif}}
main{{max-width:640px;margin:0 auto;padding:40px 20px}}
h1{{font-size:28px;margin:0 0 6px}} p.sub{{color:var(--mute);margin:0 0 28px}}
.step{{background:var(--card);border-radius:18px;padding:20px 22px;margin:14px 0}}
.step h2{{font-size:18px;margin:0 0 8px}} .step p{{margin:0 0 14px;color:var(--mute)}}
a.btn{{display:inline-block;background:var(--accent);color:#120e1a;text-decoration:none;font-weight:600;
padding:12px 22px;border-radius:12px}} a.btn.off{{background:#3a2f4d;color:var(--mute);pointer-events:none}}
code{{background:#2b2240;padding:2px 8px;border-radius:6px;font-size:15px;word-break:break-all}}
.ok{{color:#8ef0b0}}
</style></head><body><main>
<h1>魔法少女ノ魔女裁判</h1><p class="sub">把你的 Steam 版装到这台 iPad · 只在 tailnet 内可访问</p>
<div class="step"><h2>① 读取这台 iPad 的 UDID</h2>
<p>点按钮 → 允许下载描述文件 → 打开「设置」顶部的「已下载描述文件」→ 安装。装完会自动回到这里。</p>
<a class="btn" href="/udid.mobileconfig">读取 UDID</a>
<p style="margin-top:14px">{udid}</p></div>
<div class="step"><h2>② 安装游戏</h2><p>{ipa_note}</p>
<a class="btn {ipa_cls}" href="itms-services://?action=download-manifest&amp;url={base}/manifest.plist">{ipa_label}</a></div>
<div class="step"><h2>③ 打开开发者模式</h2>
<p>装好后：设置 → 隐私与安全性 → 开发者模式 → 打开，按提示重启。重启后再点主屏幕上的 manosaba。</p></div>
</main></body></html>"""


def mobileconfig() -> bytes:
    return plistlib.dumps({
        "PayloadContent": {
            "URL": f"{BASE}/udid",
            "DeviceAttributes": ["UDID", "PRODUCT", "VERSION", "DEVICE_NAME"],
        },
        "PayloadOrganization": "manosaba-ipad",
        "PayloadDisplayName": "读取 UDID（manosaba 安装用）",
        "PayloadDescription": "只把这台设备的 UDID 发给你自己的 Mac，用于登记开发设备；不会安装任何其他内容。",
        "PayloadVersion": 1,
        "PayloadUUID": str(uuid.uuid5(uuid.NAMESPACE_URL, BASE + "/udid")).upper(),
        "PayloadIdentifier": f"{BUNDLE_ID}.udid",
        "PayloadType": "Profile Service",
    })


def manifest() -> bytes:
    return plistlib.dumps({"items": [{
        "assets": [{"kind": "software-package", "url": f"{BASE}/manosaba.ipa"}],
        "metadata": {"bundle-identifier": BUNDLE_ID, "bundle-version": "1.1.2", "kind": "software", "title": "manosaba"},
    }]})


class Handler(BaseHTTPRequestHandler):
    server_version = "manosaba-ota"

    def log_message(self, fmt, *args):  # route to logging with client + path
        log.info("%s %s", self.address_string(), fmt % args)

    def _send(self, code: int, body: bytes, ctype: str, extra: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self) -> None:
        path = self.path.split("?")[0]
        if path == "/":
            udid = "还没有读取。"
            if UDID_FILE.exists():
                d = json.loads(UDID_FILE.read_text())
                udid = f'<span class="ok">已读取：</span><code>{html.escape(d.get("UDID", ""))}</code>（{html.escape(d.get("PRODUCT", ""))}，iPadOS {html.escape(d.get("VERSION", ""))}）'
            ready = IPA.exists()
            size = IPA.stat().st_size if ready else 0
            label = f"安装（{size / 2**30:.1f} GB）" if size > 2**30 else f"安装（{max(1, size // 2**20)} MB）"
            body = PAGE.format(udid=udid, base=BASE, ipa_cls="" if ready else "off", ipa_label=label,
                               ipa_note="安装包已就绪，点按钮开始安装。" if ready else "安装包准备中，登记好 UDID 后这里会变成可点击。刷新页面查看。")
            self._send(200, body.encode(), "text/html; charset=utf-8", {"Cache-Control": "no-store"})
        elif path == "/udid.mobileconfig":
            self._send(200, mobileconfig(), "application/x-apple-aspen-config")
        elif path == "/manifest.plist" and IPA.exists():
            self._send(200, manifest(), "text/xml")
        elif path == "/manosaba.ipa" and IPA.exists():
            self._send_file(IPA)
        else:
            self._send(404, b"not found", "text/plain")

    do_HEAD = do_GET

    def _send_file(self, p: Path) -> None:
        size = p.stat().st_size
        start, end = 0, size - 1
        m = re.match(r"bytes=(\d*)-(\d*)", self.headers.get("Range", ""))
        if m and (m.group(1) or m.group(2)):
            if m.group(1):
                start = int(m.group(1))
                end = int(m.group(2)) if m.group(2) else size - 1
            else:
                start = size - int(m.group(2))
        length = end - start + 1
        self.send_response(206 if m else 200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if m:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if self.command == "HEAD":
            return
        log.info("sending %s bytes %d-%d", p.name, start, end)
        with open(p, "rb") as f:
            os.sendfile(self.wfile.fileno(), f.fileno(), start, length)

    def do_POST(self) -> None:
        if self.path.split("?")[0] != "/udid":
            self._send(404, b"not found", "text/plain")
            return
        raw = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        (ROOT / "logs/ipad-udid-raw.der").write_bytes(raw)  # keep the original callback
        mt = re.search(rb"<\?xml.*?</plist>", raw, re.S)
        if not mt:
            log.error("UDID callback without plist (%d bytes)", len(raw))
            self._send(400, b"bad request", "text/plain")
            return
        attrs = plistlib.loads(mt.group(0))
        UDID_FILE.write_text(json.dumps(attrs, ensure_ascii=False, indent=1))
        log.info("UDID captured: %s %s %s", attrs.get("UDID"), attrs.get("PRODUCT"), attrs.get("VERSION"))
        self.send_response(301)
        self.send_header("Location", f"{BASE}/")
        self.end_headers()


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    (ROOT / "logs").mkdir(exist_ok=True)
    srv = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    log.info("serving on 127.0.0.1:8765 for %s", BASE)
    srv.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
