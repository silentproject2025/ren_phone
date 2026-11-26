#!/usr/bin/env python3
# relay_yt.py -- RELAY_STREAM_V1
# Relay video -> MJPEG untuk ren_phone. Jalan di Termux (atau PC/Raspberry Pi).
#
# Setup Termux (sekali):
#   pkg update && pkg install python ffmpeg nodejs
#   pip install -U yt-dlp
# Jalankan:
#   python relay_yt.py            (port 8080; ganti:  python relay_yt.py 9000)
# Pakai:
#   1. Buka http://127.0.0.1:8080 di browser HP yang menjalankan relay, tempel link YouTube, tekan Putar.
#   2. Di ren_phone: app Video -> tombol [Relay].
# Alamat relay yang dipakai ren_phone:
#   - kalau relay jalan di HP yang membuka hotspot: otomatis (gateway WiFi), tak perlu setting
#   - kalau di PC/lain: tulis "192.168.x.x:8080" di berkas /relay.txt di root SD card
# Ukuran video mengikuti panel: ren_phone mengirim /live?w=..&h=.. (Accretion v2 ST7796 = 320x480 portrait, v1 ILI9341 = 240x320).
# Hanya video, tanpa suara. Hanya satu penonton sekali waktu.
import os, re, shutil, subprocess, sys, socket, threading, urllib.parse, html
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
W, H, QUAL = 240, 320, 7                    # ukuran bawaan; ren_phone mengirim ukuran panelnya sendiri (?w=&h=). QUAL: 2 (bagus) .. 31 (kasar)
YT_RE = re.compile(r"^https?://([a-z0-9-]+\.)?(youtube\.com|youtu\.be)/", re.I)
S = {"src": "", "title": "", "rot": False, "err": "", "proc": None}
LOCK = threading.Lock()

def resolve(url):
    """link/berkas -> (sumber yg bisa dibaca ffmpeg, judul)."""
    if os.path.exists(url):
        return url, os.path.basename(url)
    if YT_RE.match(url):
        yd = shutil.which("yt-dlp")
        if not yd:
            raise RuntimeError("yt-dlp belum terpasang (pip install -U yt-dlp)")
        r = subprocess.run([yd, "--no-playlist", "-f", "b[height<=360]/bv*[height<=360]/b",
                            "--get-title", "--get-url", url],
                           capture_output=True, text=True, timeout=60)
        lines = [l.strip() for l in r.stdout.splitlines() if l.strip()]
        if r.returncode != 0 or len(lines) < 2:
            raise RuntimeError("yt-dlp gagal: " + (r.stderr.strip().splitlines() or ["tanpa pesan"])[-1][:200])
        return lines[1], lines[0]
    if re.match(r"^https?://", url, re.I):
        return url, url.rsplit("/", 1)[-1][:60]
    raise RuntimeError("bukan link http(s) / berkas yang ada")

def ffmpeg_cmd(src, rot, w=W, h=H, fps=0):
    fps = fps or (10 if w * h > 100000 else 12)      # panel besar (320x480) lebih berat didecode & dikirim
    vf = ("transpose=1," if rot else "") + \
         "fps=%d,scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black" % (fps, w, h, w, h)
    c = ["ffmpeg", "-hide_banner", "-loglevel", "error"]
    if src.startswith("http"):
        c += ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5"]
    c += ["-re", "-i", src, "-an", "-vf", vf, "-c:v", "mjpeg", "-q:v", str(QUAL),
          "-pix_fmt", "yuvj420p", "-f", "mjpeg", "pipe:1"]
    return c

def kill_proc():
    p = S["proc"]; S["proc"] = None
    if p and p.poll() is None:
        try: p.kill()
        except Exception: pass

PAGE = """<!doctype html><meta name=viewport content="width=device-width,initial-scale=1"><title>ren_phone relay</title>
<style>body{font:16px system-ui;margin:16px;max-width:520px}input[type=text]{width:100%%;padding:10px;font-size:16px;box-sizing:border-box}
button{padding:10px 16px;font-size:16px;margin:8px 8px 0 0}.m{color:#555;font-size:14px}.e{color:#b00}</style>
<h2>ren_phone relay</h2>
<form method=post action=/play><input type=text name=url placeholder="Tempel link YouTube / video" required>
<label><input type=checkbox name=rot value=1 %s> Putar 90&deg; (layar penuh untuk video lebar)</label><br>
<button>Putar</button><button formaction=/stop formnovalidate>Stop</button></form>
<p>Sedang: <b>%s</b></p><p class=e>%s</p>
<p class=m>Di ren_phone: app Video &rarr; tombol [Relay]. Hanya video, tanpa suara.</p>"""

class H_(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _page(self, code=200):
        b = (PAGE % ("checked" if S["rot"] else "", html.escape(S["title"] or "(kosong)"),
                     html.escape(S["err"]))).encode()
        self.send_response(code); self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path.startswith("/live"):
            return self.live()
        if self.path.startswith("/status"):
            b = (S["title"] or "idle").encode()
            self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b); return
        self._page()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        f = urllib.parse.parse_qs(self.rfile.read(n).decode("utf-8", "ignore"))
        with LOCK:
            if self.path.startswith("/stop"):
                S.update(src="", title="", err=""); kill_proc()
            else:
                url = (f.get("url", [""])[0]).strip()
                try:
                    src, title = resolve(url)
                    kill_proc(); S.update(src=src, title=title, rot=("rot" in f), err="")
                except Exception as e:
                    S["err"] = str(e)
        self._page()
    def live(self):
        with LOCK:
            src, rot = S["src"], S["rot"]
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            def qi(k, d, lo, hi):
                try: return max(lo, min(hi, int(q.get(k, [d])[0]))) // 2 * 2
                except Exception: return d
            w, h, fps = qi("w", W, 64, 800), qi("h", H, 64, 800), qi("fps", 0, 0, 30)
            if not src:
                b = b"idle"; self.send_response(503); self.send_header("Content-Length", "4"); self.end_headers(); self.wfile.write(b); return
            kill_proc()
            p = subprocess.Popen(ffmpeg_cmd(src, rot, w, h, fps), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
            S["proc"] = p
        try:
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.send_response(200); self.send_header("Content-Type", "video/x-motion-jpeg")
            self.send_header("Connection", "close"); self.end_headers()
            while True:
                d = p.stdout.read(16384)
                if not d: break
                self.wfile.write(d)
        except Exception:
            pass
        finally:
            try: p.kill()
            except Exception: pass

if __name__ == "__main__":
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg belum terpasang (Termux: pkg install ffmpeg)")
    print("Relay jalan di port %d. Buka http://127.0.0.1:%d di browser." % (PORT, PORT))
    ThreadingHTTPServer(("0.0.0.0", PORT), H_).serve_forever()
