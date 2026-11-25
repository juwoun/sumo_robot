# 🖥️ 씨름로봇 시뮬레이션 (ROS 2 Jazzy + Gazebo Harmonic)

SolidWorks로 설계한 씨름로봇을 ROS 2로 옮겨서 **RViz에서 모양을 확인**하고, **Gazebo에서 키보드로 몰아보는** 패키지예요.

## 📌 지금 상태 (2026-11-25)

| 항목 | 상태 |
|---|---|
| 몸체 + 바퀴 4개 모델 (SolidWorks → URDF) | ✅ 완료 |
| RViz에서 모양·바퀴 회전 확인 | ✅ 완료 |
| Gazebo에서 4륜 차동구동 + 키보드 조종 | ✅ 완료 |
| 📷 Pixy2 카메라 | ⬜ 아직 안 넣음 → [다음 할 일](#6-다음-할-일-todo) |
| 🎨 컬러센서 2개 (앞 왼쪽·오른쪽) | ⬜ 아직 안 넣음 |
| 🏟️ 경기장 월드 · 상대 로봇 | ⬜ 아직 안 넣음 |
| 🔗 실제 로봇 코드(판단 로직)와 연결 | ⬜ 아직 안 함 |

## 🧰 사용 환경

| | |
|---|---|
| 설계 | Windows 11 + SolidWorks 2026 (학생용) |
| 시뮬레이션 | Ubuntu 24.04 + ROS 2 Jazzy + Gazebo Harmonic |
| 노트북 | HP OMEN 16 (윈도우·우분투 듀얼부팅) |
| 파일 주고받기 | 이 GitHub 저장소 (윈도우: GitHub Desktop / 우분투: git) |

## 📁 폴더 구조

```
sumo_description/
├─ package.xml, CMakeLists.txt   ROS 2 패키지 설정
├─ urdf/sumo.urdf                로봇 설계도 (링크·관절·무게·Gazebo 플러그인, 한글 주석)
├─ meshes/base_link.STL          몸체 모양 (ROS 좌표로 변환된 것)
├─ meshes/wheel.STL              바퀴 모양 (4개 공용)
├─ launch/display.launch.py      RViz로 모양 확인
├─ launch/gazebo.launch.py       Gazebo 시뮬레이션
└─ rviz/display.rviz             RViz 화면 설정

tools/make_base_link/make_base_link.py   SolidWorks 부품별 STL → base_link.STL + URDF 숫자
```

---

## 1. 처음 설치 (우분투, 한 번만)

```bash
# ROS 2 Jazzy가 있는지 확인 -> jazzy 가 나오면 OK (없으면 docs.ros.org의 Jazzy 설치부터)
ls /opt/ros

# 시뮬레이션에 필요한 패키지
sudo apt update
sudo apt install -y ros-jazzy-ament-cmake ros-jazzy-ros-gz ros-jazzy-joint-state-publisher-gui \
                    ros-jazzy-teleop-twist-keyboard python3-colcon-common-extensions git

# 터미널 열 때마다 ROS 2와 워크스페이스 자동으로 불러오기
# (앞에 \n 을 꼭 넣어야 해요 - .bashrc 마지막 줄에 줄바꿈이 없으면 윗줄에 붙어버림)
printf '\nsource /opt/ros/jazzy/setup.bash\nsource ~/ros2_ws/install/setup.bash\n' >> ~/.bashrc

# 워크스페이스 만들고 저장소 받기
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws/src
git clone https://github.com/juwoun/sumo_robot.git

# 빌드
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
colcon build
source ~/.bashrc
```

`colcon build`가 `Summary: 1 package finished`로 끝나면 성공이에요. 저장소의 아두이노·파이 폴더는 `package.xml`이 없어서 자동으로 건너뛰어요.

## 2. 실행

### RViz — 모양 확인
```bash
ros2 launch sumo_description display.launch.py
```
- 회색 몸체 + 검은 바퀴 4개, 빨강(X) 축이 앞(경사진 앞판 f1 쪽)을 가리키면 정상
- 작은 슬라이더 창으로 바퀴를 하나씩 돌려볼 수 있어요

### Gazebo — 키보드로 몰아보기
```bash
# 터미널 1
ros2 launch sumo_description gazebo.launch.py

# 터미널 2 (Ctrl+Shift+T) -> 이 터미널을 클릭한 상태에서 키 입력
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

| 키 | 동작 | | 키 | 동작 |
|---|---|---|---|---|
| `i` | 전진 | | `k` | **정지** |
| `,` | 후진 | | `u` / `o` | 앞으로 가며 왼쪽 / 오른쪽 |
| `j` / `l` | 제자리 왼쪽 / 오른쪽 회전 | | `q` / `z` | 속도 10% 올리기 / 내리기 |

- 키를 한 번만 눌러도 계속 그 방향으로 가요 → 멈추려면 `k`
- **카메라가 로봇 따라가게:** Gazebo 화면에서 로봇 우클릭 → **Follow** (끄기: 빈 곳 우클릭 → Free camera)
- 카메라 거리 조절 (뒤 1 m, 위 0.7 m):
  ```bash
  gz service -s /gui/follow/offset --reqtype gz.msgs.Vector3d --reptype gz.msgs.Boolean --timeout 2000 --req 'x: -1.0, y: 0.0, z: 0.7'
  ```

---

## 3. 로봇 수치 (URDF에 들어간 값과 근거)

**좌표 규칙:** X = 앞, Y = 왼쪽, Z = 위 · 단위 m / kg
**base_link 원점:** 바퀴 4개 중심의 정가운데, 바퀴 축 높이 (바닥에서 약 0.05 m 위)

| 항목 | 값 | 근거 |
|---|---|---|
| 앞뒤 바퀴 간격 | 0.105 m (x = ±0.0525) | 어셈블리 STL 측정 |
| 좌우 바퀴 간격 (`wheel_separation`) | 0.2392 m (y = ±0.1196) | 어셈블리 STL 측정 |
| 바퀴 반지름 / 폭 | 0.05 m / 0.04 m | 바퀴 설계값 |
| 몸체 크기 (충돌 박스) | 0.302 × 0.301 × 0.121 m | 모자거치대 뺀 몸체 전체 |
| 바닥 ~ 몸체 아래 | 10.7 mm | 어셈블리 STL 측정 |
| 전체 무게 | 3.8 kg (몸체 3.4 + 바퀴 0.1 × 4) | 실측 무게, 바퀴 무게는 임시값 |
| 관성 | 몸체 = 꽉 찬 상자, 바퀴 = 원통으로 계산 | 근사값 |

| URDF 이름 | SolidWorks 부품 |
|---|---|
| `front_left_wheel` | 바퀴-4 |
| `front_right_wheel` | 바퀴-3 |
| `rear_left_wheel` | 바퀴-2 |
| `rear_right_wheel` | 바퀴-1 |

> `meshes/wheel.STL`은 지금 **단순한 원통(지름 100 × 폭 40 mm)** 이에요. 실제로 쓸 바퀴가 정해지면 같은 이름으로 덮어쓰면 돼요 (치수가 같으면 URDF는 그대로).

---

## 4. SolidWorks 모델이 바뀌었을 때 (다시 만드는 법)

공식 SolidWorks→URDF 익스포터는 SolidWorks 2021까지만 지원해서 **익스포터 없이** 했어요. SolidWorks 버전과 상관없이 되는 방법이에요.

### 4-1. 몸체가 바뀌었을 때 → `base_link.STL` 다시 만들기

**① SolidWorks 어셈블리(`cad/solidworks/어셈블리1.SLDASM`)를 부품별 STL로 저장**

파일 → 다른 이름으로 저장 → 파일 형식 **STL** → **옵션...**

| 옵션 | 설정 |
|---|---|
| 출력 형식 | Binary |
| 단위 | **m** |
| 해상도 | 거침 |
| STL 출력 데이터를 번역하지 않음 | **체크** (안 하면 위치가 멋대로 옮겨짐) |
| 어셈블리의 모든 부품을 단일 파일에 저장 | **체크 해제** (부품마다 따로 저장) |
| 출력 좌표계 | -- 기본 -- |

- 빈 폴더(예: `C:\projects\sumo_stl_export`)에 저장 → `어셈블리1 - f1-1.STL` 같은 파일들이 생겨요
- "재생성하시겠습니까?" 창이 뜨면 **재생성 없이 문서를 저장** (메이트 경고가 있으면 재생성할 때 부품이 틀어질 수 있음)
- 어셈블리 파일 자체(.SLDASM)는 저장하지 않아도 돼요

**② 스크립트 실행** (윈도우·우분투 둘 다 됨, 파이썬 3만 있으면 OK)

```bash
python3 tools/make_base_link/make_base_link.py 저장한폴더 --out sumo_description/meshes/base_link.STL --mass 3.8
```

- 화면에 **바퀴 위치, 바퀴 간격, 충돌 박스, 관성 값**이 나와요 → 바뀐 숫자가 있으면 `urdf/sumo.urdf`에서 같은 자리를 고쳐요
- 앞뒤가 반대로 나오면 `--front +x` 를 붙여서 다시 실행 (기본값은 `-x` = SolidWorks에서 -X 방향이 앞)
- 부품별 STL들은 다 쓰면 지워도 돼요 (`base_link.STL` 안에 합쳐져 있음)

**③ 우분투에서 다시 빌드**
```bash
cd ~/ros2_ws && colcon build && source ~/.bashrc
```

### 4-2. 바퀴가 바뀌었을 때 → `wheel.STL` 다시 만들기

바퀴 부품(`바퀴.SLDPRT`)에서 **바퀴 정중앙에, Y축이 바퀴 축 방향인 좌표계**를 만들고 그 좌표계 기준으로 STL을 저장해요. (지금 `바퀴.SLDPRT`에는 `좌표계1`이 이미 들어 있어요.)

1. **기준면**: 참조 형상 → 기준면 → 바퀴 양쪽 옆면 선택 → 가운데 평면 (`평면1`)
2. **축**: 참조 형상 → 축 → 바퀴 바깥 원통면 선택 (`기준축1`)
3. **점**: 참조 형상 → 점 → 교차점 → `기준축1` + `평면1` (`점1` = 바퀴 정중앙)
4. **좌표계**: 참조 형상 → 좌표계 → 원점 `점1`, **Y축 `기준축1`** (`좌표계1`)
5. **STL 저장**: 옵션은 4-1과 같고, **출력 좌표계 = 좌표계1** → `sumo_description/meshes/wheel.STL`

확인: STL 크기가 X·Z = 지름, Y = 폭, 중심이 (0, 0, 0)이면 정상이에요.

### 참고: SolidWorks에서 겪은 것
- 어셈블리 안에서만 만든 가상 부품(`[파트1^어셈블리1]`처럼 대괄호로 표시)은 STL로 안 나와요 → 화면 모양에서만 빠지고 물리 계산엔 영향 없음
- 부품을 지우거나 억제하면 그 부품에 걸린 메이트가 경고(노랑)/오류(빨강)가 돼요 → 메이트를 새로 걸기 전에, 메이트 폴더에서 지운 부품 이름이 들어간 메이트부터 삭제
- 부품을 지울 땐 삭제 대신 **우클릭 → 억제** (언제든 되돌릴 수 있음)

---

## 5. 🐛 문제 해결

| 증상 | 원인 | 해결 |
|---|---|---|
| `colcon build`에서 `Could not find ... "ament_cmake"` | 터미널에 ROS 2 환경이 안 불러와짐 / 빌드 도구 미설치 | `sudo apt install ros-jazzy-ament-cmake` → `source /opt/ros/jazzy/setup.bash` → `rm -rf build install log` → 다시 `colcon build` |
| 새 터미널에서 `bash: alias: ...setup.bash: not found`, `ros2: command not found` | `.bashrc` 마지막 줄에 줄바꿈이 없어서 추가한 `source` 줄이 윗줄(`alias`)에 붙음 | 아래 명령으로 고치기 |
| Gazebo에서 로봇 몸체 모양이 안 보임 | Gazebo가 STL 경로를 못 찾음 | `gazebo.launch.py`로 켰는지 확인 (이 파일이 `GZ_SIM_RESOURCE_PATH`를 설정함), `colcon build` 다시 |
| 키보드를 눌러도 안 움직임 | 조종 터미널이 선택 안 됨 | 조종 터미널을 마우스로 클릭한 뒤 키 입력 |
| 제자리 회전이 굼뜸 | 4륜 스키드 스티어라 바퀴가 옆으로 미끄러져야 돎 | `urdf/sumo.urdf`의 바퀴 `mu2`를 0.5 정도로 낮추거나 `wheel_separation`을 키워서 보정 |

`.bashrc` 고치는 명령:
```bash
sed -i 's#source /opt/ros/jazzy/setup.bash##g; s#source ~/ros2_ws/install/setup.bash##g' ~/.bashrc
printf '\nsource /opt/ros/jazzy/setup.bash\nsource ~/ros2_ws/install/setup.bash\n' >> ~/.bashrc
source ~/.bashrc
echo $ROS_DISTRO   # jazzy 가 나오면 성공
```

---

## 6. 다음 할 일 (TODO)

### 📷 Pixy2 카메라 넣기
- **할 것:** URDF에 `camera_link` 추가 + Gazebo 카메라 센서 → `ros_gz_bridge`로 영상 받기 → OpenCV로 초록 모자·빨간 원 찾기 (Pixy2 시그니처 1·2 흉내)
- **필요한 값 (실물/SolidWorks에서 재기):**
  - [ ] 카메라 렌즈 위치: base_link 원점(바퀴 4개 정중앙, 바퀴 축 높이)에서 앞 x / 왼쪽 y / 위 z (mm)
  - [ ] 카메라 기울기: 아래로 몇 도 숙였는지
  - [ ] 시야각·해상도: Pixy2 기본값 수평 약 60°, 수직 약 40°, 316 × 208 (확인 필요)
  - `cad/solidworks/카메라 거치대.SLDPRT`를 어셈블리에 넣고 다시 뽑으면 화면 모양에도 나와요

### 🎨 컬러센서 2개 (앞 왼쪽 FL · 앞 오른쪽 FR)
- **할 것:** 바닥을 보는 아주 작은 카메라 센서(예: 8 × 8 픽셀) 2개 → 평균 색으로 빨강/파랑/기타 판단 (실제 컬러센서 흉내)
- **필요한 값:**
  - [ ] 센서 2개 각각의 위치: 앞 x / 좌우 y (mm)
  - [ ] 바닥에서 센서까지 높이 (mm)

### 🏟️ 경기장 월드 · 상대 로봇
- [ ] IRC 씨름로봇 경기장 규격 확인: 경기장 지름, 바닥 색, 테두리 색(빨강·파랑), 중앙 빨간 원 지름
- [ ] `worlds/sumo_arena.sdf` 만들기 (경기장 + 중앙 원)
- [ ] 초록 모자 쓴 상대 로봇 하나 더 띄우기 (같은 URDF + 모자)

### 🔗 실제 코드와 연결
- [ ] `robot_pi/robot_pi.py`·`robot_arduino`의 판단 로직(바닥 회피 → 탐색 → 우회 → 추적 → 돌진)을 ROS 2 노드로 옮겨서 `/cmd_vel`로 시뮬 로봇 움직이기
- [ ] 대회 당일처럼 2륜(대각선) 구동도 시뮬로 비교해 보기

### ⚙️ 그 밖에
- [ ] 실제 바퀴 무게 재서 URDF의 `0.1` 바꾸기
- [ ] 실제 바퀴 모양 정해지면 `wheel.STL` 교체

---

## 🔄 윈도우 ↔ 우분투 작업 순서

| 어디서 | 작업 전 | 작업 후 |
|---|---|---|
| 윈도우 (GitHub Desktop) | **Fetch origin → Pull** | Commit → **Push origin** |
| 우분투 (터미널) | `cd ~/ros2_ws/src/sumo_robot && git pull` | `git add -A && git commit -m "내용" && git push` |

> 한쪽에서 Push 안 하고 다른 쪽에서 작업하면 충돌이 나요. **시작할 때 Pull, 끝날 때 Push.**
