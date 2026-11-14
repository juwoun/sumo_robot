/*
 * robot_arduino.ino — IRC 씨름로봇 (Arduino Mega 2560)
 * 배선: 씨름로봇 배선도 REV B 기준 (엔코더 없음, 컬러센서 앞 2개만 사용)
 *
 * 시작: D42 토글 스위치 ON → 오른쪽 회전부터 시작, OFF → 즉시 정지
 *       (전원 넣을 때 이미 ON 이면 한 번 OFF→ON 해야 시작)
 *
 * 판단 우선순위 (위가 먼저)
 *   1. 바닥 회피 : 앞 센서 중 하나라도 파랑 또는 빨강 감지 → 탈출 동작
 *                  FL만 → 짧게 후진 후 오른쪽 회전
 *                  FR만 → 짧게 후진 후 왼쪽 회전
 *                  둘 다 → 조금 길게 후진 후 크게 회전(마지막으로 상대 본 쪽)
 *                  후진은 색이 안 보일 때까지 (+ESC_BACK_EXTRA_MS), 최대 ESC_BACK_MAX_MS
 *                  ※ 뒤 센서가 없으므로 최대 후진 시간으로 제한
 *   2. 탐색      : 초록 모자가 안 보이면 제자리 회전
 *                  (전원 ON 직후엔 오른쪽, 이후엔 마지막으로 본 방향)
 *   3. 우회      : 빨간 원이 초록 모자 앞을 가리면 원 가장자리 바깥을 조준
 *   4. 돌진      : 초록 모자가 크게 보이면(가까우면) 최대 출력
 *   5. 추적      : 초록 모자 x 오차로 PID 조향
 *   (빨강 한쪽만 밟아도 1번 회피 동작을 함 — 2026-11-10 변경)
 *
 * Pi → Mega 시리얼 (USB, 115200bps), 한 줄씩:
 *   "V,g,gx,gy,gw,r,rx,ry,rw,rh\n"
 *   g / r : 초록 모자 / 빨간 원 보이면 1, 안 보이면 0
 *   x     : -100(화면 왼쪽 끝) ~ +100(오른쪽 끝), 0 = 화면 중앙
 *   y     : 0(화면 위) ~ 100(화면 아래)
 *   w     : x와 같은 스케일의 폭 (200 = 화면 전체 폭)
 *   h     : y와 같은 스케일의 높이
 * Mega → Pi : '#'으로 시작하는 줄은 디버그 출력 (robot_pi.py가 화면에 찍어줌)
 */

// ===================== 디버그 =====================
// 1로 바꾸면 모터를 멈추고 컬러센서 원시값만 출력 (임계값 맞출 때 사용)
#define COLOR_DEBUG 0
// 1로 바꾸면 주행하면서 100ms마다 현재 상태 출력
#define STATE_DEBUG 1

// ===================== 시작 토글 스위치 =====================
// D42 에 토글 스위치. 스위치 반대쪽 단자가
//   GND 에 연결  → SW_TO_GND 1 (기본, 내부 풀업 사용, ON = LOW)
//   5V  에 연결  → SW_TO_GND 0 (D42–GND 사이에 10kΩ 풀다운 저항 필요, ON = HIGH)
#define SW_TO_GND 1
const uint8_t START_SW = 42;
const unsigned long SW_DEBOUNCE_MS = 30;

// ===================== 핀 (배선도 기준) =====================
// 2026-11-13 motor_check 실측: 앞/뒤 바퀴가 핀과 반대로 연결돼 있어서 핀 이름을 실제 위치에 맞춤
//   왼쪽 앞 = 7/26, 왼쪽 뒤 = 5/22, 오른쪽 앞 = 6/24, 오른쪽 뒤 = 8/28 (왼쪽만 앞뒤 바뀜)
const uint8_t PWM_FL = 7, DIR_FL = 26;
const uint8_t PWM_RL = 5, DIR_RL = 22;
const uint8_t PWM_FR = 6, DIR_FR = 24;   // 2026-11-13 오른쪽은 원래 핀이 맞음
const uint8_t PWM_RR = 8, DIR_RR = 28;
// 바퀴 하나가 반대로 돌면 그 바퀴만 true/false 반대로
const bool INV_FL = false, INV_RL = true, INV_FR = false, INV_RR = true;   // 2026-11-13 motor_check 결과 반영

// ★ 임시 우회: FR 은 DIR 신호가 안 들어가서(D24→MDD10A#2 DIR1 배선 문제) 항상 뒤로만 돎.
//   true 면 FR 은 '뒤로 가야 할 때'만 돌리고, 앞으로 가야 할 때는 끔 (나머지 3바퀴로 전진).
//   (2026-11-13 핀 재배치 후 FR = 8/28. 이 기능은 꺼져 있음)
const bool FR_DIR_BROKEN = false;   // 2026-11-10 D24 선 다시 꽂음 → 해제

// TCS3200 컬러센서 2개 (앞 왼쪽 FL, 앞 오른쪽 FR) — S0~S3 공통, OUT 개별
// (REV A 의 RL=D40, RR=D41 은 이제 사용 안 함 → 비워두기)
const uint8_t CS_S0 = 34, CS_S1 = 35, CS_S2 = 36, CS_S3 = 37;
const uint8_t NUM_CS = 2;
const uint8_t CS_OUT[NUM_CS] = {38, 39};  // FL, FR
const uint8_t FL = 0, FR = 1;
const char* CS_NAME[NUM_CS] = {"FL", "FR"};

// ===================== 튜닝 값 =====================
const unsigned long START_DELAY_MS = 0;   // 스위치 ON 후 출발 대기 (규정에 대기시간 있으면 입력)

// 속도 (0~255)
const int SEARCH_SPEED = 75;    // 제자리 회전 탐색 (130→90→75. 안 돌면 80~90)

// ----- 반응 속도 개선 (2026-11-11) -----
// ① 탐색 중 모자를 처음 보면 반대로 짧게 돌려서 회전 관성(반동)을 끊음
const int BRAKE_PWR = 120;              // 브레이크 세기
const unsigned long BRAKE_MS = 50;      // 브레이크 시간 (오버슈트 남으면 늘리고, 반대로 튀면 줄이기)
// ② 모자가 옆에 있으면 먼저 제자리에서 돌아 조준하고, 가운데로 올수록 전진 속도를 올림
const int AIM_FULL_X = 50;              // |x| 가 이 이상이면 전진 0 (제자리 조준)
const int MIN_PIVOT = 80;               // 조준할 때 최소 회전 출력 (모터가 실제로 도는 최소값보다 크게)
const int AIM_DEAD = 8;                 // |x| 가 이 안이면 가운데로 봄
const int TRACK_SPEED  = 170;   // 추적
const int DETOUR_SPEED = 150;   // 우회
const int PUSH_SPEED   = 255;   // 돌진

// PID (입력: x 오차 -100~100, 출력: 좌우 속도 차)
const float KP = 1.2f;
const float KI = 0.0f;
const float KD = 0.04f;
const float I_LIMIT = 50.0f;
const int TURN_LIMIT = 150;       // 추적·우회 때 최대 조향값
const int PUSH_TURN_LIMIT = 80;   // 돌진 때 최대 조향값 (너무 꺾으면 밀리는 힘 손실)

// 비전 판단 기준 (단위는 위 시리얼 형식 참고)
const int CLOSE_W = 60;            // 초록 모자 폭이 이 이상이면 '가까움' → 돌진
const int RED_MIN_W = 30;          // 빨간 블록 폭이 이보다 작으면 멀리 있는 것으로 보고 무시
const int RED_NEAR_BOTTOM = 70;    // 빨간 블록 아래 끝(y)이 이 이상이면 로봇 앞에 있다고 판단
const int BLOCK_MARGIN = 10;       // 초록 x가 빨간 블록 좌우 범위 ± 이 값 안이면 '막음'
const int DETOUR_MARGIN = 25;      // 우회 때 빨간 가장자리에서 바깥으로 비켜 조준할 양
const unsigned long VISION_TIMEOUT_MS = 200;  // Pi 데이터가 이 시간 이상 안 오면 '안 보임' 처리

// 앞 센서 하나가 빨강 밟았을 때 살짝 조향 (원 가장자리 따라 돌기)
const int RED_NUDGE_FRONT = 90;

// 바닥 회피 동작 (속도 0~255, 시간 ms)
const int ESC_TURN = 150;        // 2026-11-10: 200→150 (회전 시간은 늘려서 각도 유지)
const int ESC_BACK = 200;
// 후진: 센서가 색을 더 이상 안 볼 때까지 + 여유 시간만큼 더. 단, 최대 시간은 넘지 않음
const unsigned int ESC_BACK_EXTRA_MS = 100;  // 색이 사라진 뒤 조금 더 후진하는 시간
const unsigned int ESC_BACK_MAX_MS   = 600;  // 후진 최대 시간 (뒤 센서 없음 → 뒤로 떨어지지 않게 제한)
const unsigned int ESC_TURN_MS       = 270;  // 한쪽만 감지: 회전 시간
const unsigned int ESC_TURN_BOTH_MS  = 500;  // 둘 다 감지: 크게 회전 (약 180°, 실측으로 조정)

// 컬러센서 판정 (COLOR_DEBUG 로 실제 경기장에서 값을 보고 맞추기)
const unsigned long CS_TIMEOUT_US = 2000;
const float BLACK_SUM  = 30.0f;   // 밝기 합(R+G+B)이 이보다 작으면 검정
const float RED_RATIO  = 0.45f;   // R 비율이 이보다 크면 빨강 후보
const float BLUE_RATIO = 0.50f;   // 임시: FR 이 책상 위에서 0.42~0.43 으로 읽혀 파랑 오판 → 경기장 보정 때 다시 맞추기

// ===================== 내부 상태 =====================
const uint8_t F_BLACK = 0, F_RED = 1, F_BLUE = 2, F_OTHER = 3;
const char* F_NAME[4] = {"BLK", "RED", "BLU", "OTH"};
uint8_t floorNow[NUM_CS], floorCand[NUM_CS], candCount[NUM_CS];
unsigned int rawR[NUM_CS], rawG[NUM_CS], rawB[NUM_CS];

// 센서 비트마스크
const uint8_t B_FL = 1, B_FR = 2;

const uint8_t M_SEARCH = 0, M_ESCAPE = 1, M_DETOUR = 2, M_PUSH = 3, M_TRACK = 4, M_HOLD = 5, M_BRAKE = 6;
const char* M_NAME[7] = {"SEARCH", "ESCAPE", "DETOUR", "PUSH", "TRACK", "HOLD", "BRAKE"};
uint8_t mode = M_SEARCH;

struct Vision {
  bool g; int gx, gy, gw;
  bool r; int rx, ry, rw, rh;
  unsigned long t;
};
Vision vis = {false, 0, 0, 0, false, 0, 0, 0, 0, 0};

char rxBuf[64];
uint8_t rxLen = 0;

// ----- 마지막으로 본 초록 모자 기억 -----
// 시작은 오른쪽 회전, 이후엔 모자를 마지막으로 본 쪽(움직이던 쪽)으로 회전
int lastSeenDir = 1;   // +1 = 오른쪽, -1 = 왼쪽. 전원 ON 직후 오른쪽 회전
const int DIR_DEADBAND = 10;            // 모자가 가운데(±10) 근처면 방향 기억을 안 바꿈 (흔들림 방지)
const int PREDICT_K = 3;                // 모자가 움직이던 속도 × 이 값만큼 앞을 예측
const unsigned long PUSH_HOLD_MS = 300; // 돌진 중 모자가 사라지면 이 시간 동안은 계속 밀기 (너무 가까워 화면 밖)
int spinDir = 1;                        // 탐색 때 실제로 돌던 방향
unsigned long brakeUntil = 0;           // 이 시각까지 브레이크
unsigned long lastGreenT = 0, lastVisT = 0;
int prevGx = 0;
bool prevGreen = false;
int detourSide = 1;

// 회피 상태: 0 = 안 함, 1 = 후진 중, 2 = 회전 중
const uint8_t ESC_NONE = 0, ESC_BACKING = 1, ESC_TURNING = 2;
uint8_t escPhase = ESC_NONE, escMask = 0;
int escTurnDir = 1;                 // +1 = 오른쪽 회전, -1 = 왼쪽 회전
unsigned int escTurnMs = 0;
unsigned long escPhaseStart = 0, escClearT = 0;
int escL = 0, escR = 0;             // 현재 회피 출력 (디버그용)

float iTerm = 0, prevErr = 0;
unsigned long prevPidT = 0, lastPidVisT = 0;
bool pidFresh = true;
int turnCmd = 0;

// ===================== 모터 =====================
void setMotor(uint8_t pwmPin, uint8_t dirPin, bool inv, int spd) {
  spd = constrain(spd, -255, 255);
  if (inv) spd = -spd;
  digitalWrite(dirPin, spd >= 0 ? HIGH : LOW);
  analogWrite(pwmPin, spd >= 0 ? spd : -spd);
}

// 왼쪽/오른쪽 속도 (-255~255). 범위를 넘으면 비율 유지하며 줄임
void drive(int l, int r) {
  int al = l < 0 ? -l : l;
  int ar = r < 0 ? -r : r;
  int m = al > ar ? al : ar;
  if (m > 255) {
    l = (int)((long)l * 255 / m);
    r = (int)((long)r * 255 / m);
  }
  setMotor(PWM_FL, DIR_FL, INV_FL, l);
  setMotor(PWM_RL, DIR_RL, INV_RL, l);
  if (FR_DIR_BROKEN) {
    // FR 은 방향 제어가 안 되고 항상 뒤로 돎 → 뒤로(r<0)일 때만 출력, 앞으로일 땐 0
    analogWrite(PWM_FR, r < 0 ? -r : 0);
  } else {
    setMotor(PWM_FR, DIR_FR, INV_FR, r);
  }
  setMotor(PWM_RR, DIR_RR, INV_RR, r);
}

// ===================== 컬러센서 =====================
void selectFilter(uint8_t s2, uint8_t s3) {
  digitalWrite(CS_S2, s2);
  digitalWrite(CS_S3, s3);
  delayMicroseconds(50);
}

unsigned int readPulse(uint8_t pin) {
  unsigned long p = pulseIn(pin, LOW, CS_TIMEOUT_US);
  return p == 0 ? CS_TIMEOUT_US : (unsigned int)p;
}

uint8_t classify(unsigned int r, unsigned int g, unsigned int b) {
  float R = 1000.0f / r, G = 1000.0f / g, B = 1000.0f / b;
  float s = R + G + B;
  if (s < BLACK_SUM) return F_BLACK;
  float rn = R / s, gn = G / s, bn = B / s;
  if (rn > RED_RATIO && gn < 0.30f) return F_RED;
  if (bn > BLUE_RATIO && rn < 0.30f) return F_BLUE;
  return F_OTHER;
}

void readSerial();

// 컬러센서를 읽는 사이사이 시리얼 수신도 처리해서 카메라 데이터가 밀리지 않게 함
void readFloor() {
  selectFilter(LOW, LOW);   for (uint8_t i = 0; i < NUM_CS; i++) rawR[i] = readPulse(CS_OUT[i]);
  readSerial();
  selectFilter(HIGH, HIGH); for (uint8_t i = 0; i < NUM_CS; i++) rawG[i] = readPulse(CS_OUT[i]);
  readSerial();
  selectFilter(LOW, HIGH);  for (uint8_t i = 0; i < NUM_CS; i++) rawB[i] = readPulse(CS_OUT[i]);
  readSerial();

  // 같은 색이 2번 연속 나와야 확정 (노이즈 방지)
  for (uint8_t i = 0; i < NUM_CS; i++) {
    uint8_t f = classify(rawR[i], rawG[i], rawB[i]);
    if (f == floorCand[i]) {
      if (candCount[i] < 2) candCount[i]++;
    } else {
      floorCand[i] = f;
      candCount[i] = 1;
    }
    if (candCount[i] >= 2) floorNow[i] = f;
  }
}

// ===================== Pi 데이터 =====================
void parseLine(char* s) {
  if (s[0] != 'V' || s[1] != ',') return;
  int v[9];
  uint8_t n = 0;
  char* p = s + 2;
  while (n < 9) {
    v[n++] = atoi(p);
    p = strchr(p, ',');
    if (!p) break;
    p++;
  }
  if (n != 9) return;
  vis.g = v[0];  vis.gx = v[1]; vis.gy = v[2]; vis.gw = v[3];
  vis.r = v[4];  vis.rx = v[5]; vis.ry = v[6]; vis.rw = v[7]; vis.rh = v[8];
  vis.t = millis();
  if (vis.t == 0) vis.t = 1;
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      rxBuf[rxLen] = 0;
      parseLine(rxBuf);
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;  // 줄이 너무 길면 버림
    }
  }
}

// ===================== 바닥 회피 =====================
// 감지된 센서 조합으로 회전 방향·시간 정하기 (앞 센서 2개 → 경우 3가지)
void planTurn(uint8_t m) {
  escMask = m;
  if (m == B_FL)      { escTurnDir = 1;  escTurnMs = ESC_TURN_MS; }       // 왼쪽 앞 → 오른쪽 회전
  else if (m == B_FR) { escTurnDir = -1; escTurnMs = ESC_TURN_MS; }       // 오른쪽 앞 → 왼쪽 회전
  else                { escTurnDir = lastSeenDir; escTurnMs = ESC_TURN_BOTH_MS; }  // 정면 → 크게 회전
}

// 회피: ① 색이 안 보일 때까지 후진 (+여유, 최대 ESC_BACK_MAX_MS) → ② 반대쪽으로 회전
// 회피 동작 중이면 true
bool runEscape(uint8_t hard) {
  unsigned long now = millis();

  if (hard) {
    if (escPhase == ESC_NONE) {                 // 새로 감지 → 후진 시작
      planTurn(hard);
      escPhase = ESC_BACKING;
      escPhaseStart = now;
    } else if (escPhase == ESC_BACKING && (hard & ~escMask)) {
      planTurn(hard | escMask);                 // 후진 중 다른 쪽도 감지 → 회전 계획만 갱신 (후진 시간은 이어서)
    }
    // 회전 중에 감지된 건 무시하고 회전을 끝까지 함 (끝난 뒤에도 보이면 다시 후진)
  }

  if (escPhase == ESC_BACKING) {
    if (hard) escClearT = 0;
    else if (escClearT == 0) escClearT = now;   // 색이 처음 사라진 시각
    bool cleared = escClearT != 0 && now - escClearT >= ESC_BACK_EXTRA_MS;
    bool tooLong = now - escPhaseStart >= ESC_BACK_MAX_MS;
    if (cleared || tooLong) {
      escPhase = ESC_TURNING;
      escPhaseStart = now;
    } else {
      escL = -ESC_BACK; escR = -ESC_BACK;
      drive(escL, escR);
      return true;
    }
  }

  if (escPhase == ESC_TURNING) {
    if (now - escPhaseStart >= escTurnMs) {
      escPhase = ESC_NONE;
      escMask = 0;
      escClearT = 0;
      return false;
    }
    escL = escTurnDir * ESC_TURN; escR = -escTurnDir * ESC_TURN;
    drive(escL, escR);
    return true;
  }
  return false;
}

int redNudge() {
  int n = 0;
  if (floorNow[FL] == F_RED) n += RED_NUDGE_FRONT;
  if (floorNow[FR] == F_RED) n -= RED_NUDGE_FRONT;
  return n;   // + = 오른쪽으로 조향
}

// ===================== PID =====================
void resetPid() {
  iTerm = 0;
  pidFresh = true;
}

int pid(float err) {
  unsigned long now = millis();
  float d = 0;
  if (!pidFresh) {
    float dt = (now - prevPidT) / 1000.0f;
    if (dt > 0) {
      iTerm = constrain(iTerm + err * dt, -I_LIMIT, I_LIMIT);
      d = (err - prevErr) / dt;
    }
  }
  prevErr = err;
  prevPidT = now;
  pidFresh = false;
  float out = KP * err + KI * iTerm + KD * d;
  return (int)constrain(out, (float)-TURN_LIMIT, (float)TURN_LIMIT);
}

bool setMode(uint8_t m) {
  if (m == mode) return false;
  mode = m;
  resetPid();
  return true;
}

// ===================== 시작 스위치 =====================
bool swRaw() {
#if SW_TO_GND
  return digitalRead(START_SW) == LOW;
#else
  return digitalRead(START_SW) == HIGH;
#endif
}

bool running = false;
bool swStable = false, swLast = false;
unsigned long swChangeT = 0, runStartT = 0;

// 스위치 상태 갱신. 주행해도 되면 true
bool updateSwitch() {
  bool s = swRaw();
  unsigned long now = millis();
  if (s != swLast) {
    swLast = s;
    swChangeT = now;
  }
  if (s != swStable && now - swChangeT >= SW_DEBOUNCE_MS) {
    swStable = s;
    if (s) {
      // OFF → ON : 처음 상태로 초기화 후 시작
      running = true;
      runStartT = now;
      lastSeenDir = 1;   // 시작은 항상 오른쪽 회전
      lastGreenT = 0; lastVisT = 0; prevGreen = false;
      escPhase = ESC_NONE;
      escMask = 0;
      escClearT = 0;
      mode = M_SEARCH;
      resetPid();
      Serial.println("# START");
    } else {
      running = false;
      Serial.println("# STOP");
    }
  }
  return running && (now - runStartT >= START_DELAY_MS);
}

// ===================== 디버그 출력 =====================
void printColorDebug() {
  static unsigned long last = 0;
  if (millis() - last < 200) return;
  last = millis();
  Serial.print('#');
  for (uint8_t i = 0; i < NUM_CS; i++) {
    Serial.print(' '); Serial.print(CS_NAME[i]); Serial.print(' ');
    Serial.print(rawR[i]); Serial.print('/');
    Serial.print(rawG[i]); Serial.print('/');
    Serial.print(rawB[i]); Serial.print('=');
    Serial.print(F_NAME[floorNow[i]]);
  }
  Serial.println();
}

void printStateDebug(int l, int r) {
  static unsigned long last = 0;
  if (millis() - last < 100) return;
  last = millis();
  Serial.print("# "); Serial.print(M_NAME[mode]);
  Serial.print(" g="); Serial.print(vis.g); Serial.print(' '); Serial.print(vis.gx);
  Serial.print(','); Serial.print(vis.gw);
  Serial.print(" r="); Serial.print(vis.r); Serial.print(' '); Serial.print(vis.rx);
  Serial.print(','); Serial.print(vis.rw);
  Serial.print(" floor=");
  for (uint8_t i = 0; i < NUM_CS; i++) {
    // 판정 + 원시값(R/G/B 펄스폭) 같이 출력 → 주행 중에도 임계값 보정 가능
    Serial.print(CS_NAME[i]); Serial.print(':'); Serial.print(F_NAME[floorNow[i]]);
    Serial.print('('); Serial.print(rawR[i]); Serial.print('/'); Serial.print(rawG[i]);
    Serial.print('/'); Serial.print(rawB[i]); Serial.print(") ");
  }
  Serial.print("L="); Serial.print(l); Serial.print(" R="); Serial.println(r);
}

// ===================== setup / loop =====================
void setup() {
  const uint8_t motorPins[8] = {PWM_FL, DIR_FL, PWM_RL, DIR_RL, PWM_FR, DIR_FR, PWM_RR, DIR_RR};
  for (uint8_t i = 0; i < 8; i++) pinMode(motorPins[i], OUTPUT);
  drive(0, 0);

  pinMode(CS_S0, OUTPUT); pinMode(CS_S1, OUTPUT);
  pinMode(CS_S2, OUTPUT); pinMode(CS_S3, OUTPUT);
  digitalWrite(CS_S0, HIGH);   // 출력 주파수 20% 스케일
  digitalWrite(CS_S1, LOW);
  for (uint8_t i = 0; i < NUM_CS; i++) {
    pinMode(CS_OUT[i], INPUT);
    floorNow[i] = F_BLACK;
    floorCand[i] = F_BLACK;
    candCount[i] = 0;
  }

#if SW_TO_GND
  pinMode(START_SW, INPUT_PULLUP);
#else
  pinMode(START_SW, INPUT);
#endif

  Serial.begin(115200);

  // 전원 넣을 때 스위치가 이미 ON 이면 바로 출발하지 않음 (안전)
  // → 한 번 OFF 했다가 ON 해야 시작
  swStable = swLast = swRaw();
  if (swStable) Serial.println("# 스위치가 ON 상태로 켜짐: OFF 후 다시 ON 하면 시작");
}

void loop() {
  readSerial();
  readFloor();
  readSerial();

#if COLOR_DEBUG
  drive(0, 0);
  printColorDebug();
  return;
#endif

  if (!updateSwitch()) {
    drive(0, 0);   // 스위치 OFF (또는 출발 대기 중) → 정지
    // 스위치 OFF 동안은 컬러센서 값을 출력 → 로봇을 바닥에 내려놓고 보정 가능
    if (!running) printColorDebug();
    return;
  }

  unsigned long now = millis();
  bool visOk = vis.t != 0 && (now - vis.t) < VISION_TIMEOUT_MS;
  bool green = visOk && vis.g;
  if (green) {
    // 새 카메라 프레임일 때만 기억 갱신
    if (vis.t != lastVisT) {
      int pred = vis.gx;
      if (prevGreen) pred += (vis.gx - prevGx) * PREDICT_K;   // 모자가 움직이던 쪽으로 예측
      if (pred > DIR_DEADBAND)       lastSeenDir = 1;   // 오른쪽에서 사라질 것 같음 → 오른쪽으로
      else if (pred < -DIR_DEADBAND) lastSeenDir = -1;  // 왼쪽 → 왼쪽으로
      // 가운데면 이전 방향 유지
      prevGx = vis.gx;
      lastVisT = vis.t;
    }
    lastGreenT = now;
  }
  prevGreen = green;

  uint8_t blueMask = 0, redMask = 0;
  for (uint8_t i = 0; i < NUM_CS; i++) {
    if (floorNow[i] == F_BLUE) blueMask |= (1 << i);
    if (floorNow[i] == F_RED)  redMask  |= (1 << i);
  }

  // 1. 바닥 회피
  // 파랑(경계) 또는 빨강(중앙 원) 중 하나라도 밟으면 → 후진 후 반대쪽으로 회전
  //   FL만 → 후진 후 오른쪽 회전 / FR만 → 후진 후 왼쪽 회전 / 둘 다 → 길게 후진 후 크게 회전
  uint8_t hard = blueMask | redMask;
  if (runEscape(hard)) {
    setMode(M_ESCAPE);
#if STATE_DEBUG
    printStateDebug(escL, escR);
#endif
    return;
  }

  // 2-1. 돌진하다가 모자가 사라짐 → 너무 가까워서 화면 밖으로 나간 것. 잠깐 계속 밀기
  if (!green && (mode == M_PUSH || mode == M_HOLD) && now - lastGreenT < PUSH_HOLD_MS) {
    setMode(M_HOLD);
    drive(PUSH_SPEED, PUSH_SPEED);
#if STATE_DEBUG
    printStateDebug(PUSH_SPEED, PUSH_SPEED);
#endif
    return;
  }

  // 2-2. 탐색: 처음엔 오른쪽, 이후엔 마지막으로 본 쪽으로 제자리 회전
  if (!green) {
    setMode(M_SEARCH);
    spinDir = lastSeenDir;
    int l = spinDir * SEARCH_SPEED, r = -spinDir * SEARCH_SPEED;
    drive(l, r);
#if STATE_DEBUG
    printStateDebug(l, r);
#endif
    return;
  }

  // 2-3. 탐색하다가 방금 모자를 찾음 → 짧게 반대로 돌려서 회전 관성(반동) 끊기
  if (mode == M_SEARCH) brakeUntil = now + BRAKE_MS;
  if ((long)(brakeUntil - now) > 0) {
    setMode(M_BRAKE);
    int l = -spinDir * BRAKE_PWR, r = spinDir * BRAKE_PWR;
    drive(l, r);
#if STATE_DEBUG
    printStateDebug(l, r);
#endif
    return;
  }

  // 3. 빨간 원이 앞을 막는지 판단
  bool blocked = false;
  int aim = vis.gx;
  if (vis.r && vis.rw >= RED_MIN_W && (vis.ry + vis.rh / 2) >= RED_NEAR_BOTTOM) {
    int left = vis.rx - vis.rw / 2;
    int right = vis.rx + vis.rw / 2;
    if (vis.gx >= left - BLOCK_MARGIN && vis.gx <= right + BLOCK_MARGIN) {
      blocked = true;
      if (mode != M_DETOUR) detourSide = vis.gx >= vis.rx ? 1 : -1;  // 우회 방향은 처음에 한 번만 정함
      aim = detourSide > 0 ? right + DETOUR_MARGIN : left - DETOUR_MARGIN;
      aim = constrain(aim, -100, 100);
    }
  }

  uint8_t m = blocked ? M_DETOUR : (vis.gw >= CLOSE_W ? M_PUSH : M_TRACK);
  bool changed = setMode(m);

  // PID는 새 카메라 데이터가 왔을 때만 갱신
  if (changed || vis.t != lastPidVisT) {
    turnCmd = pid((float)aim);
    lastPidVisT = vis.t;
  }

  int base = (m == M_DETOUR) ? DETOUR_SPEED : (m == M_PUSH ? PUSH_SPEED : TRACK_SPEED);
  int turn = turnCmd;
  if (m == M_PUSH) {
    turn = constrain(turn, -PUSH_TURN_LIMIT, PUSH_TURN_LIMIT);
  } else {
    // 조준 먼저: 모자가 옆에 있을수록 전진을 줄이고 제자리에서 돌아 맞춤
    int ax = abs(aim);
    float k = 1.0f - (float)ax / AIM_FULL_X;
    if (k < 0) k = 0;
    base = (int)(base * k);
    if (ax > AIM_DEAD && abs(turn) < MIN_PIVOT && base < MIN_PIVOT)
      turn = (aim > 0 ? 1 : -1) * MIN_PIVOT;   // 너무 약하면 바퀴가 안 돌아서 반응이 굼뜸
  }
  turn += redNudge();

  int l = base + turn, r = base - turn;
  drive(l, r);
#if STATE_DEBUG
  printStateDebug(l, r);
#endif
}
