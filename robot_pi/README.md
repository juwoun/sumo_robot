# robot_pi — 라즈베리 파이 코드

> [!NOTE]
> 이 폴더의 `robot_pi.py` 는 **재작성본**이에요.
> 대회 때 쓴 원본(`robot_pi.py`, `viewer.py`)은 라즈베리 파이의 `~/robot_pi` 에 있고, 나중에 원본으로 바꿀 예정이에요.
> Mega 코드에 적힌 통신 규칙에 맞춰 같은 동작을 하도록 다시 작성했어요.

## 하는 일

```
[Pixy2] ──USB──▶ [Raspberry Pi 4] ──USB 시리얼 115200──▶ [Arduino Mega]
                  robot_pi.py
                  ① 초록 모자(시그니처 1) · 빨간 원(시그니처 2) 블록 읽기
                  ② 가장 큰 블록 위치·크기를 -100~100 으로 변환
                  ③ "V,g,gx,gy,gw,r,rx,ry,rw,rh" 한 줄씩 전송 (초당 30번)
```

## 실행

```bash
sudo apt install python3-serial
python3 robot_pi.py
```

- Pixy2 파이썬 모듈이 필요해요 (pixy2 소스를 빌드해서 나온 `pixy.py`, `_pixy.so`)
- 32비트 Raspberry Pi OS에서 확인됨 (64비트에서는 블록 값이 깨졌음)
- 대회 때는 systemd 서비스(`robot_pi`)로 등록해서 전원을 켜면 자동 실행
