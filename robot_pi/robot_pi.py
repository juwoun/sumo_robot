#!/usr/bin/env python3
"""
robot_pi.py — IRC 씨름로봇 라즈베리 파이 쪽 코드 (재작성본)

※ 대회 때 쓴 원본은 라즈베리 파이의 ~/robot_pi 에 있음.
   이 파일은 Mega 코드(robot_arduino.ino)에 적힌 통신 규칙에 맞춰 다시 작성한 버전.

하는 일
  1. Pixy2 카메라에서 색 블록 읽기
       시그니처 1 = 상대 초록 모자, 시그니처 2 = 경기장 중앙 빨간 원
  2. 가장 큰 블록의 위치·크기를 -100~100 스케일로 바꿔서
  3. Arduino Mega로 한 줄씩 보내기 (USB 시리얼, 115200bps)
       "V,g,gx,gy,gw,r,rx,ry,rw,rh\n"
         g / r : 초록 모자 / 빨간 원 보이면 1, 안 보이면 0
         x     : -100(왼쪽 끝) ~ +100(오른쪽 끝), 0 = 화면 가운데
         y     : 0(위) ~ 100(아래)
         w     : x와 같은 스케일의 폭 (200 = 화면 전체 폭)
         h     : y와 같은 스케일의 높이
  4. Mega가 보내는 '#' 디버그 줄은 화면에 출력

실행
  python3 robot_pi.py
  (systemd 서비스 robot_pi 로 등록하면 전원 켤 때 자동 실행)

필요한 것
  - Pixy2 파이썬 모듈 (pixy2 소스의 build/python_demos 에서 만든 pixy.py, _pixy.so)
  - pyserial  (sudo apt install python3-serial)
"""

import sys
import time

import serial

try:
    import pixy
    from pixy import BlockArray
except ImportError:
    sys.exit("pixy 모듈이 없어요. pixy2/build/python_demos 폴더에서 실행하거나 경로를 추가하세요.")

# ===================== 설정 =====================
SERIAL_PORT = "/dev/ttyUSB0"   # Mega (CH340 호환보드)
BAUD = 115200

SIG_GREEN = 1                  # PixyMon에서 학습한 시그니처 번호: 초록 모자
SIG_RED = 2                    # 빨간 원

FRAME_W = 316                  # Pixy2 화면 크기
FRAME_H = 208

SEND_HZ = 30                   # Mega로 보내는 횟수 (초당). Mega는 200ms 동안 안 오면 '안 보임' 처리
MIN_AREA = 40                  # 이보다 작은 블록은 잡음으로 무시 (픽셀 넓이)
MAX_BLOCKS = 50


# ===================== 함수 =====================
def to_x(px):
    """Pixy x 픽셀(0~316) → -100~100"""
    return int((px - FRAME_W / 2) * 200 / FRAME_W)


def to_y(py):
    """Pixy y 픽셀(0~208) → 0~100"""
    return int(py * 100 / FRAME_H)


def to_w(pw):
    return int(pw * 200 / FRAME_W)


def to_h(ph):
    return int(ph * 100 / FRAME_H)


def biggest(blocks, count, sig):
    """시그니처가 sig 인 블록 중 가장 큰 것 하나 (없으면 None)"""
    best, best_area = None, MIN_AREA
    for i in range(count):
        b = blocks[i]
        if b.m_signature != sig:
            continue
        area = b.m_width * b.m_height
        if area > best_area:
            best, best_area = b, area
    return best


def make_line(green, red):
    """Mega로 보낼 한 줄 만들기"""
    if green:
        g = (1, to_x(green.m_x), to_y(green.m_y), to_w(green.m_width))
    else:
        g = (0, 0, 0, 0)
    if red:
        r = (1, to_x(red.m_x), to_y(red.m_y), to_w(red.m_width), to_h(red.m_height))
    else:
        r = (0, 0, 0, 0, 0)
    return "V," + ",".join(str(v) for v in g + r) + "\n"


def open_serial():
    """Mega 연결. 안 되면 1초마다 다시 시도 (부팅 직후 USB가 늦게 잡힐 수 있음)"""
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0)
            time.sleep(2)              # Mega는 포트를 열면 리셋됨 → 부팅 기다리기
            print("# Mega 연결됨:", SERIAL_PORT)
            return ser
        except serial.SerialException:
            print("# Mega 연결 대기 중...")
            time.sleep(1)


def print_mega_debug(ser, buf):
    """Mega가 보낸 '#' 줄을 화면에 출력"""
    data = ser.read(256)
    if not data:
        return buf
    buf += data.decode(errors="ignore")
    while "\n" in buf:
        line, buf = buf.split("\n", 1)
        line = line.strip()
        if line.startswith("#"):
            print("[Mega]", line)
    return buf


# ===================== 메인 =====================
def main():
    if pixy.init() < 0:
        sys.exit("Pixy2를 찾을 수 없어요. USB 연결을 확인하세요.")
    pixy.change_prog("color_connected_components")
    print("# Pixy2 준비 완료")

    ser = open_serial()
    blocks = BlockArray(MAX_BLOCKS)
    period = 1.0 / SEND_HZ
    rx_buf = ""

    while True:
        t0 = time.time()

        count = pixy.ccc_get_blocks(MAX_BLOCKS, blocks)
        if count < 0:
            count = 0
        green = biggest(blocks, count, SIG_GREEN)
        red = biggest(blocks, count, SIG_RED)

        try:
            ser.write(make_line(green, red).encode())
            rx_buf = print_mega_debug(ser, rx_buf)
        except serial.SerialException:
            print("# Mega 연결 끊김 → 다시 연결")
            ser = open_serial()

        dt = time.time() - t0
        if dt < period:
            time.sleep(period - dt)


if __name__ == "__main__":
    main()
