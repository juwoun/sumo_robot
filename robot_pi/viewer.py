"""
viewer.py — 카메라 확인 화면 (http://<파이IP>:8080)

robot_pi.py 가 부르는 함수:
  start(port)        : 웹 서버 시작 (백그라운드)
  update(**state)    : 최신 상태 저장 (pixy, serial, blocks, fw, fh, line, fps)
  mega_line(text)    : Mega 디버그 줄 저장 (최근 30줄)
"""

import json
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_lock = threading.Lock()
_state = {"pixy": False, "serial": False, "blocks": [], "fw": 316, "fh": 208,
          "line": "", "fps": 0.0, "t": 0.0}
_mega = deque(maxlen=30)

PAGE = """<!doctype html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>씨름로봇 카메라</title>
<style>
body{margin:0;font-family:sans-serif;background:#111;color:#eee;padding:12px}
h1{font-size:18px;margin:0 0 8px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px}
.tag{padding:3px 8px;border-radius:4px;font-size:13px;background:#333}
.ok{background:#1e7d3a}.bad{background:#a12a2a}
canvas{width:100%;max-width:632px;background:#000;border:1px solid #444;display:block}
pre{background:#000;border:1px solid #333;padding:8px;font-size:12px;max-width:616px;
    height:220px;overflow:auto;white-space:pre-wrap}
</style></head><body>
<h1>씨름로봇 카메라 / 상태</h1>
<div class="row">
 <span id="px" class="tag">Pixy</span><span id="sr" class="tag">Mega</span>
 <span id="fps" class="tag">fps</span>
</div>
<canvas id="c" width="632" height="416"></canvas>
<div class="row" style="margin-top:8px"><span class="tag" id="ln">-</span></div>
<b>Mega 디버그</b>
<pre id="mg"></pre>
<script>
const c=document.getElementById('c'),g=c.getContext('2d');
const COL={1:'#2ecc71',2:'#e74c3c'},NAME={1:'초록 모자',2:'빨간 원'};
async function tick(){
 try{
  const s=await (await fetch('/state')).json();
  px.className='tag '+(s.pixy?'ok':'bad'); px.textContent='Pixy '+(s.pixy?'연결':'없음');
  sr.className='tag '+(s.serial?'ok':'bad'); sr.textContent='Mega '+(s.serial?'연결':'없음');
  fps.textContent=s.fps.toFixed(0)+' fps';
  ln.textContent=s.line||'-';
  const kx=c.width/s.fw, ky=c.height/s.fh;
  g.fillStyle='#000'; g.fillRect(0,0,c.width,c.height);
  g.strokeStyle='#333'; g.beginPath(); g.moveTo(c.width/2,0); g.lineTo(c.width/2,c.height); g.stroke();
  if(!s.blocks.length){g.fillStyle='#777';g.font='16px sans-serif';g.fillText('블록 없음',12,24);}
  for(const [sig,x,y,w,h] of s.blocks){
   g.strokeStyle=COL[sig]||'#fff'; g.lineWidth=3;
   g.strokeRect((x-w/2)*kx,(y-h/2)*ky,w*kx,h*ky);
   g.fillStyle=COL[sig]||'#fff'; g.font='14px sans-serif';
   g.fillText((NAME[sig]||('sig'+sig))+' '+w+'x'+h,(x-w/2)*kx+4,(y-h/2)*ky-6);
  }
  mg.textContent=s.mega.join('\\n'); mg.scrollTop=mg.scrollHeight;
 }catch(e){}
 setTimeout(tick,100);
}
tick();
</script></body></html>"""


class _H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/state"):
            with _lock:
                body = json.dumps(dict(_state, mega=list(_mega))).encode()
            ctype = "application/json"
        else:
            body = PAGE.encode()
            ctype = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def start(port=8080):
    try:
        srv = ThreadingHTTPServer(("0.0.0.0", port), _H)
    except OSError as e:
        print(f"[viewer] 시작 실패: {e}", flush=True)
        return
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"[viewer] http://<파이IP>:{port}", flush=True)


def update(**kw):
    with _lock:
        _state.update(kw)
        _state["t"] = time.time()


def mega_line(text):
    with _lock:
        _mega.append(text)
