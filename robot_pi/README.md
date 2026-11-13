# robot_pi — 라즈베리 파이 코드

> [!NOTE]
> 2026-11-12 노트북에 백업해 둔 원본이에요. 대회 직전 파이에서 더 고친 부분이 있으면 파이의 `~/robot_pi` 버전으로 바꿔 넣기.

| 파일 | 하는 일 |
|---|---|
| `robot_pi.py` | Pixy2에서 초록 모자(시그니처 1)·빨간 원(시그니처 2)을 찾아 Mega로 `V,g,gx,gy,gw,r,rx,ry,rw,rh` 전송, Mega의 `#` 디버그 줄 출력 |
| `viewer.py` | 카메라 인식 결과 확인 화면 (`http://<파이IP>:8080`) |
| `setup_pi.sh` | 파이 설치 스크립트 (Pixy2 라이브러리 빌드, 서비스 등록) |
| `robot_pi.service.template` | 전원 켜면 자동 실행되는 systemd 서비스 설정 |

```
[Pixy2] ──USB──▶ [Raspberry Pi 4] ──USB 시리얼 115200──▶ [Arduino Mega]
```

- 32비트 Raspberry Pi OS에서 동작 확인 (64비트에서는 Pixy2 블록 값이 깨졌음)
- 로그 보기: `journalctl -u robot_pi -f`
