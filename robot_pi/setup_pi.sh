#!/bin/bash
# setup_pi.sh — IRC 씨름로봇 파이 설치 (Raspberry Pi 4, 32비트 Raspberry Pi OS, Pixy2)
# 사용법:  cd ~/robot_pi && chmod +x setup_pi.sh && ./setup_pi.sh
# 인터넷(핫스팟 데이터) 필요. 처음 한 번만 실행.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
USER_NAME="$(whoami)"

echo "=== 1/5 패키지 설치 ==="
sudo apt update
sudo apt install -y git build-essential cmake libusb-1.0-0-dev swig \
    python3-dev python3-setuptools python3-serial

echo "=== 2/5 Pixy2 라이브러리 받기 ==="
if [ ! -d "$HOME/pixy2" ]; then
    git clone --depth 1 https://github.com/charmedlabs/pixy2.git "$HOME/pixy2"
fi

echo "=== 3/5 Pixy2 파이썬 모듈 빌드 (몇 분 걸림) ==="
cd "$HOME/pixy2/scripts"
./build_libpixyusb2.sh
./build_python_demos.sh

# 빌드된 pixy 파이썬 모듈을 robot_pi 폴더로 복사
PYDIR="$HOME/pixy2/build/python_demos"
cp "$PYDIR"/pixy.py "$DIR"/
cp "$PYDIR"/_pixy*.so "$DIR"/

echo "=== 4/5 USB 권한 (Pixy, Mega) ==="
sudo cp "$HOME/pixy2/src/host/linux/pixy.rules" /etc/udev/rules.d/ 2>/dev/null || \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="b1ac", MODE="0666"' | sudo tee /etc/udev/rules.d/pixy.rules >/dev/null
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG dialout,plugdev "$USER_NAME"

echo "=== 5/5 자동 실행 서비스 등록 ==="
sed -e "s|__USER__|$USER_NAME|g" -e "s|__DIR__|$DIR|g" \
    "$DIR/robot_pi.service.template" | sudo tee /etc/systemd/system/robot_pi.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable robot_pi
sudo systemctl restart robot_pi

IP="$(hostname -I | awk '{print $1}')"
echo ""
echo "설치 완료!"
echo "  로그 보기     : journalctl -u robot_pi -f"
echo "  카메라 화면   : http://$IP:8080"
echo "  (권한 적용을 위해 한 번 재부팅 권장: sudo reboot)"
