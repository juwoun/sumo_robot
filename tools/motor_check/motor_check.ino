/*
 * motor_check.ino — 모터 방향·좌우 확인 (Arduino Mega 2560)
 *
 * 기준: 로봇 "뒤에 서서" 카메라가 보는 방향을 같이 볼 때
 *   FL = 왼쪽 앞, RL = 왼쪽 뒤, FR = 오른쪽 앞, RR = 오른쪽 뒤
 *   "앞으로" = 바닥에 놓으면 카메라 쪽으로 가는 방향 (바퀴 윗부분이 카메라 쪽으로 굴러감)
 *
 * 사용법
 *   1. 바퀴를 띄운다. (모터 리포 연결)
 *   2. 업로드 → 토글(D42) ON 하면 시작, OFF 하면 즉시 정지
 *   3. 각 단계 2초 동작 + 1초 정지, 끝나면 반복
 *   4. 지금 단계가 시리얼 모니터(115200)에 나옴
 *      (파이에 꽂혀 있으면: journalctl -u robot_pi -f 에도 나옴)
 *
 * [A] 바퀴 하나씩 "앞으로" — 이름에 적힌 그 바퀴만, 앞으로 돌아야 정상
 *   → 다른 바퀴가 돌면 : 핀 배선이 바뀐 것 (어느 바퀴가 돌았는지 기록)
 *   → 맞는 바퀴가 뒤로 돌면 : 그 바퀴 INV_ 값을 반대로(true↔false)
 * [B] 묶음 동작 — 실제 주행처럼 움직여야 정상
 *
 * 아래 핀·INV_ 값은 수정된 robot_arduino.ino (2026-11-13) 와 같음. 고친 값은 robot_arduino.ino 에도 똑같이 넣을 것.
 */

// ---- robot_arduino.ino 와 똑같이 맞출 것 ----
const uint8_t PWM_FL = 7, DIR_FL = 26;   // 2026-11-13 실측으로 앞/뒤 핀 교체
const uint8_t PWM_RL = 5, DIR_RL = 22;
const uint8_t PWM_FR = 6, DIR_FR = 24;   // 2026-11-13 오른쪽은 원래 핀이 맞음
const uint8_t PWM_RR = 8, DIR_RR = 28;
const bool INV_FL = false, INV_RL = true, INV_FR = false, INV_RR = true;
// ------------------------------------------

const uint8_t SW_PIN = 42;      // 토글: D42 - 스위치 - GND
const int SPEED = 120;          // 안 돌면 150 정도로 올리기

void setMotor(uint8_t pwmPin, uint8_t dirPin, bool inv, int spd) {
  spd = constrain(spd, -255, 255);
  if (inv) spd = -spd;
  digitalWrite(dirPin, spd >= 0 ? HIGH : LOW);
  analogWrite(pwmPin, spd >= 0 ? spd : -spd);
}

void wheels(int fl, int rl, int fr, int rr) {
  setMotor(PWM_FL, DIR_FL, INV_FL, fl);
  setMotor(PWM_RL, DIR_RL, INV_RL, rl);
  setMotor(PWM_FR, DIR_FR, INV_FR, fr);
  setMotor(PWM_RR, DIR_RR, INV_RR, rr);
}

bool switchOn() { return digitalRead(SW_PIN) == LOW; }

// ms 동안 유지. 도중에 토글 OFF 면 즉시 정지하고 false
bool hold(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (!switchOn()) { wheels(0, 0, 0, 0); return false; }
    while (Serial.available()) Serial.read();   // 파이 카메라 데이터는 버림
    delay(5);
  }
  return true;
}

bool step(const char* name, int fl, int rl, int fr, int rr) {
  Serial.print("# [모터 테스트] ");
  Serial.println(name);
  wheels(fl, rl, fr, rr);
  if (!hold(2000)) return false;
  wheels(0, 0, 0, 0);
  return hold(1000);
}

void setup() {
  const uint8_t pins[8] = {PWM_FL, DIR_FL, PWM_RL, DIR_RL, PWM_FR, DIR_FR, PWM_RR, DIR_RR};
  for (uint8_t i = 0; i < 8; i++) pinMode(pins[i], OUTPUT);
  pinMode(SW_PIN, INPUT_PULLUP);
  wheels(0, 0, 0, 0);
  Serial.begin(115200);
  Serial.println("# [모터 테스트] 준비됨 - 바퀴 띄우고 토글 ON");
}

void loop() {
  if (!switchOn()) {
    wheels(0, 0, 0, 0);
    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 1000) {
      lastMsg = millis();
      Serial.println("# [모터 테스트] 대기 중 (토글 ON 하면 시작)");
    }
    while (Serial.available()) Serial.read();
    return;
  }
  const int S = SPEED;
  // [A] 하나씩
  if (!step("A1) FL 왼쪽 앞  -> 앞으로", S, 0, 0, 0)) return;
  if (!step("A2) RL 왼쪽 뒤  -> 앞으로", 0, S, 0, 0)) return;
  if (!step("A3) FR 오른쪽 앞 -> 앞으로", 0, 0, S, 0)) return;
  if (!step("A4) RR 오른쪽 뒤 -> 앞으로", 0, 0, 0, S)) return;
  // [B] 묶음
  if (!step("B1) 왼쪽 2개만 앞으로",       S, S, 0, 0)) return;
  if (!step("B2) 오른쪽 2개만 앞으로",     0, 0, S, S)) return;
  if (!step("B3) 4개 전진",               S, S, S, S)) return;
  if (!step("B4) 4개 후진",              -S, -S, -S, -S)) return;
  if (!step("B5) 오른쪽 제자리회전 (탐색) = 왼쪽 앞, 오른쪽 뒤", S, S, -S, -S)) return;
  if (!step("B6) 왼쪽 제자리회전 = 왼쪽 뒤, 오른쪽 앞",          -S, -S, S, S)) return;
  Serial.println("# [모터 테스트] --- 한 바퀴 끝, 반복 ---");
}
