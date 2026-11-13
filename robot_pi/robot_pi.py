#!/usr/bin/env python3
"""
robot_pi.py — IRC 씨름로봇 (Raspberry Pi 5)

Pixy 카메라에서 초록 모자(시그니처 1)와 빨간 원(시그니처 2)을 찾아
Arduino Mega로 USB 시리얼 전송한다.

보내는 형식 (한 줄): "V,g,gx,gy,gw,r,rx,ry,rw,rh\n"
  g / r : 보이면 1, 안 보이면 0
  x     : -100(왼쪽) ~ +100(오른쪽), 0 = 화면 중앙
  y     : 0(위) ~ 100(아래)
  w     : x와 같은 스케일의 폭 (200 = 화면 전체 폭)
  h     : y와 같은 스케일의 높이

Mega가 '#'으로 시작하는 줄을 보내면 디버그 출력으로 보고 화면에 찍는다.
Mega 전원(토글 스위치)이 꺼져 있으면 포트가 사라지므로, 다시 생길 때까지 계속 찾는다.
"""

import glob
import sys
import time

import serial

try:
    import viewer   # 카메라 분석 화면 (http://<파이IP>:8080), 없어도 로봇은 동작
except ImportError:
    class viewer:   # viewer.py 가 없으면 아무것도 안 하는 대체품
        start = update = mega_line = staticmethod(lambda *a, **k: None)

SIG_GREEN = 1        # PixyMon에서 초록 모자를 학습시킨 시그니처 번호
SIG_RED = 2          # 빨간 원 시그니처 번호
BAUD = 115200
LOOP_HZ = 60
MIN_BLOCK_AREA = 100     # 이보다 작은 블록(폭×높이, 픽셀)은 노이즈로 보고 무시. 박스가 많으면 ↑
MEGA_BOOT_WAIT_S = 2.5   # 포트 연 뒤 Mega 부트로더가 끝날 때까지 전송 안 함
PIXY_HOLD_S = 0.1   # Pixy1은 새 프레임이 없으면 0개를 주므로, 이 시간 동안은 직전 결과 유지


PIXY_ERR_LIMIT = 30   # 연속 오류 이 횟수(약 0.5초) 넘으면 Pixy 재연결


class PixyLost(Exception):
    pass


class PixyCam:
    """Pixy2(libpixyusb2)와 Pixy1/CMUcam5(libpixyusb) 파이썬 모듈을 모두 지원."""

    def __init__(self):
        import pixy
        self.pixy = pixy
        self.v2 = hasattr(pixy, "ccc_get_blocks")
        if self.v2:
            if pixy.init() < 0:
                raise RuntimeError("Pixy2 init 실패")
            pixy.change_prog("color_connected_components")
            self.width, self.height = 316, 208
        else:
            if pixy.pixy_init() != 0:
                raise RuntimeError("Pixy init 실패")
            self.width, self.height = 320, 200
        self.blocks = pixy.BlockArray(100)
        self.err_count = 0
        self.last = []
        self.last_t = 0.0

    def get_blocks(self):
        """[(signature, x, y, w, h), ...] 픽셀 단위."""
        now = time.monotonic()
        if self.v2:
            n = self.pixy.ccc_get_blocks(100, self.blocks)
            if n < 0:
                # "error: no response" — Pixy USB 통신이 끊김 (뺐다 꽂음, 전원 흔들림 등)
                # 계속되면 PixyLost 를 올려서 main 루프가 Pixy 만 다시 연결하게 함
                self.err_count += 1
                if self.err_count >= PIXY_ERR_LIMIT:
                    raise PixyLost(f"Pixy 응답 없음 {self.err_count}회 연속")
                return []
            self.err_count = 0
            out = [(b.m_signature, b.m_x, b.m_y, b.m_width, b.m_height)
                   for b in (self.blocks[i] for i in range(n))]
            return out
        n = self.pixy.pixy_get_blocks(100, self.blocks)
        if n < 0:
            raise RuntimeError(f"pixy_get_blocks 오류 {n}")
        if n == 0:
            return self.last if now - self.last_t < PIXY_HOLD_S else []
        self.last = [(b.signature, b.x, b.y, b.width, b.height)
                     for b in (self.blocks[i] for i in range(n))]
        self.last_t = now
        return self.last


def largest(blocks, sig):
    cand = [b for b in blocks if b[0] == sig]
    return max(cand, key=lambda b: b[3] * b[4]) if cand else None


def normalize(b, fw, fh):
    _, x, y, w, h = b
    return (round((x - fw / 2) * 200 / fw),
            round(y * 100 / fh),
            round(w * 200 / fw),
            round(h * 100 / fh))


def make_line(blocks, fw, fh):
    g = largest(blocks, SIG_GREEN)
    r = largest(blocks, SIG_RED)
    gx, gy, gw, _ = normalize(g, fw, fh) if g else (0, 0, 0, 0)
    rx, ry, rw, rh = normalize(r, fw, fh) if r else (0, 0, 0, 0)
    return f"V,{int(bool(g))},{gx},{gy},{gw},{int(bool(r))},{rx},{ry},{rw},{rh}\n"


def find_port():
    # 정품 Mega = ttyACM*, CH340 칩 호환 Mega = ttyUSB*
    ports = (sorted(glob.glob("/dev/serial/by-id/*Arduino*"))
             + sorted(glob.glob("/dev/ttyACM*"))
             + sorted(glob.glob("/dev/ttyUSB*")))
    return ports[0] if ports else None


def open_serial(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0
    s.write_timeout = 0.05
    s.dtr = False   # 포트 열 때 Mega가 리셋되는 것을 최대한 줄임
    s.rts = False
    s.open()
    return s


def main():
    viewer.start(8080)

    cam = None
    while cam is None:
        try:
            cam = PixyCam()
            print(f"[pixy] 연결됨 ({'Pixy2' if cam.v2 else 'Pixy1'}, {cam.width}x{cam.height})", flush=True)
        except Exception as e:
            print(f"[pixy] 대기 중: {e}", flush=True)
            viewer.update(pixy=False)
            time.sleep(1)

    ser = None
    ser_waiting = False
    ser_ready_at = 0.0
    rx_buf = b""
    next_port_check = 0.0
    next_pixy_retry = 0.0
    period = 1.0 / LOOP_HZ
    fps = 0.0
    last_t = time.monotonic()

    while True:
        t0 = time.monotonic()
        fps += 0.1 * (1.0 / max(t0 - last_t, 1e-3) - fps)
        last_t = t0

        if ser is None and t0 >= next_port_check:
            port = find_port()
            if port:
                try:
                    ser = open_serial(port)
                    rx_buf = b""
                    # 포트를 열면 Mega가 재부팅됨. 부팅 직후 데이터를 보내면
                    # Mega 부트로더가 업로드로 착각해 코드가 시작되지 않으므로 잠깐 기다림
                    ser_ready_at = t0 + MEGA_BOOT_WAIT_S
                    ser_waiting = True
                    print(f"[serial] {port} 연결됨 (Mega 부팅 대기 {MEGA_BOOT_WAIT_S:.1f}초)", flush=True)
                except (serial.SerialException, OSError) as e:
                    print(f"[serial] 열기 실패: {e}", flush=True)
                    ser = None
            next_port_check = t0 + 0.5

        try:
            blocks = cam.get_blocks()
        except PixyLost as e:
            # 프로그램은 그대로 두고 Pixy 만 다시 연결 (시리얼은 유지 → Mega 리셋 안 됨)
            blocks = []
            if t0 >= next_pixy_retry:
                print(f"[pixy] {e} → 다시 연결 시도", flush=True)
                try:
                    if cam.pixy.init() >= 0:
                        cam.pixy.change_prog("color_connected_components")
                        cam.err_count = 0
                        print("[pixy] 다시 연결됨", flush=True)
                except Exception as e2:
                    print(f"[pixy] 재연결 실패: {e2}", flush=True)
                next_pixy_retry = t0 + 1.0
        except Exception as e:
            print(f"[pixy] 읽기 오류: {e}", flush=True)
            blocks = []
            time.sleep(0.2)

        # 초록(1)·빨강(2) 시그니처만, 너무 작은 블록(노이즈)은 버림
        blocks = [b for b in blocks if b[0] in (SIG_GREEN, SIG_RED) and b[3] * b[4] >= MIN_BLOCK_AREA]

        line = make_line(blocks, cam.width, cam.height)

        if ser is not None and ser_waiting:
            if t0 >= ser_ready_at:
                ser_waiting = False
                try:
                    ser.reset_input_buffer()
                except (serial.SerialException, OSError):
                    pass
                print("[serial] Mega 준비 완료, 전송 시작", flush=True)

        if ser is not None and not ser_waiting:
            try:
                ser.write(line.encode())
                data = ser.read(256)
                if data:
                    rx_buf += data
                    while b"\n" in rx_buf:
                        msg, rx_buf = rx_buf.split(b"\n", 1)
                        if msg.startswith(b"#"):
                            text = msg[1:].decode(errors="replace").strip()
                            print("[mega] " + text, flush=True)
                            viewer.mega_line(text)
                    if len(rx_buf) > 1024:
                        rx_buf = b""
            except (serial.SerialException, OSError):
                print("[serial] 끊김 (Mega 전원 OFF?) — 다시 찾는 중", flush=True)
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None

        viewer.update(pixy=cam.err_count < PIXY_ERR_LIMIT, serial=ser is not None, blocks=[list(b) for b in blocks],
                      fw=cam.width, fh=cam.height, line=line.strip(), fps=fps)

        if "--show" in sys.argv:
            print(line.strip(), flush=True)

        dt = time.monotonic() - t0
        if dt < period:
            time.sleep(period - dt)


if __name__ == "__main__":
    main()
