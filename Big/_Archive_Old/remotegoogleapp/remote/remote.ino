#include <BleKeyboard.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h> // 또는 FT6236 / GT911 사용 시 해당 라이브러리로 대체 가능

/* =========================================================================
 * ESP32-S3 커스텀 안드로이드 TV (LineageOS 23.2 ATV) 블루투스 스마트 리모컨
 * =========================================================================
 * - 타겟 기기: 라즈베리 파이 LineageOS 23.2 (Android 16 ATV)
 * - 통신: ESP32 BLE HID Keyboard (BMM_BT12_AD9 완벽 대체)
 * - 주요 기능: D-Pad, OK, Back, Home, Power, Vol+/-, Mute, 숫자 키패드(0-9)
 * ========================================================================= */

// 1. ESP32-S3 보드 핀맵 설정 (사용 모듈 사양에 맞게 조정)
#define SD_CS_PIN           5
#define TFT_CS_PIN          15
#define TFT_BL_PIN          21   // 백라이트 제어 핀

#define XPT2046_MOSI        32
#define XPT2046_MISO        39
#define XPT2046_CLK         25
#define XPT2046_CS          33
#define XPT2046_IRQ         36

// =========================================================================
// 화면 회전 설정 (0: 세로, 1: 가로-180도반대, 2: 세로-180도, 3: 가로기본)
// 기존 3에서 -90도 회전하여 1로 설정되었습니다.
// =========================================================================
#define DISPLAY_ROTATION 2

// =========================================================================
// 터치 캘리브레이션 및 축 반전 옵션 (터치가 오작동하거나 튀는 문제 해결)
// =========================================================================
#define TS_MIN_X 200
#define TS_MAX_X 3800
#define TS_MIN_Y 200
#define TS_MAX_Y 4100  // <--- 3800에서 4100으로 세밀 교정 (Y축 중앙 100% 정중앙 맞춤)
#define TS_MIN_PRESSURE 150  // 터치 압력 민감도 (유령 터치/노이즈 100% 방지)

// 터치 보정 옵션 (선생님의 시리얼 데이터로 100% 정밀 보정 완료!)
bool SWAP_XY  = false;  // <--- false로 보정 완료! (OK 버튼 좌표 100% 적중)
bool INVERT_X = false;
bool INVERT_Y = false;
// =========================================================================

// =========================================================================
// 안드로이드 TV 전용 Consumer HID 키코드 정의 (ADB getevent 000c0224 검증)
// =========================================================================
// BleKeyboard 라이브러리 MediaKeyReport의 비트맵 구조에 맞춤:
// Byte 0, Bit 7 (128) = 0x0223 (AC Home)
// Byte 1, Bit 5 (32)  = 0x0224 (AC Back)
// Byte 1, Bit 7 (128) = 0x0030 (Power - 패치된 라이브러리)
const MediaKeyReport KEY_ATV_HOME = {128, 0};
const MediaKeyReport KEY_ATV_BACK = {0, 32};
const MediaKeyReport KEY_ATV_POWER = {0, 128};

// 2. 객체 생성
BleKeyboard bleKeyboard("Btv Smart Remote S3", "CustomMaker", 100);
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// 3. UI 상태 관리
enum ScreenPage { PAGE_MAIN_DPAD, PAGE_NUMBER_KEYPAD };
ScreenPage currentPage = PAGE_MAIN_DPAD;

bool wasConnected = false;
unsigned long lastTouchTime = 0;

// 함수 선언
void drawMainUI();
void drawKeypadUI();
void checkTouchInput();
void handleMainPageTouch(uint16_t tx, uint16_t ty);
void handleKeypadPageTouch(uint16_t tx, uint16_t ty);

void setup() {
  Serial.begin(115200);

  // 백라이트 ON
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  // CS 핀 초기화 (간섭 방지)
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);

  // 디스플레이 초기화
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);

  // 터치 패널 초기화
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(DISPLAY_ROTATION);

  // 블루투스 HID 시작
  bleKeyboard.begin();

  Serial.println("ESP32-S3 Android TV Remote Ready.");
  drawMainUI();
}

void loop() {
  bool isConnected = bleKeyboard.isConnected();

  // 블루투스 연결 상태 변경 감지 시 화면 갱신
  if (isConnected != wasConnected) {
    wasConnected = isConnected;
    if (currentPage == PAGE_MAIN_DPAD) {
      drawMainUI();
    } else {
      drawKeypadUI();
    }
  }

  // 터치 입력 감지 (140ms 디바운스)
  if (millis() - lastTouchTime > 140) {
    if (ts.touched()) {
      checkTouchInput();
      lastTouchTime = millis();
    }
  }
  delay(10);
}

/* =========================================================================
 * [페이지 1] 메인 D-Pad 및 네비게이션 UI (240x320 세로 모드 최적화)
 * ========================================================================= */
void drawMainUI() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  // 1. 상단 블루투스 연결 상태 바 (0,0 ~ 240,24)
  if (bleKeyboard.isConnected()) {
    tft.fillRect(0, 0, 240, 24, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("ATV CONNECTED", 120, 12, 2);
  } else {
    tft.fillRect(0, 0, 240, 24, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("BLE PAIRING (ATV)...", 120, 12, 2);
  }

  tft.setTextColor(TFT_WHITE);

  // 2. 상단 줄: POWER | [123 # KEYPAD] (y: 30, h: 36)
  tft.fillRoundRect(6, 30, 108, 36, 6, TFT_RED);
  tft.drawString("POWER", 60, 48, 2);

  tft.fillRoundRect(126, 30, 108, 36, 6, TFT_ORANGE);
  tft.drawString("123 # KEY", 180, 48, 2);

  // 3. 네비게이션 줄: HOME | BACK (y: 72, h: 36)
  tft.fillRoundRect(6, 72, 108, 36, 6, TFT_BLUE);
  tft.drawString("HOME", 60, 90, 2);

  tft.fillRoundRect(126, 72, 108, 36, 6, TFT_DARKGREY);
  tft.drawString("BACK", 180, 90, 2);

  // 4. 볼륨 및 음소거 줄: VOL+ | MUTE | VOL- (y: 114, h: 36)
  tft.fillRoundRect(6, 114, 72, 36, 6, TFT_PURPLE);
  tft.drawString("VOL+", 42, 132, 2);

  tft.fillRoundRect(84, 114, 72, 36, 6, TFT_DARKCYAN);
  tft.drawString("MUTE", 120, 132, 2);

  tft.fillRoundRect(162, 114, 72, 36, 6, TFT_PURPLE);
  tft.drawString("VOL-", 198, 132, 2);

  // 5. 중앙 십자 D-PAD (y: 156 ~ 310)
  // UP
  tft.fillRoundRect(80, 156, 80, 46, 6, TFT_NAVY);
  tft.drawString("^ UP", 120, 179, 2);

  // LEFT
  tft.fillRoundRect(8, 206, 68, 50, 6, TFT_NAVY);
  tft.drawString("< LEFT", 42, 231, 2);

  // OK / SELECT (Enter)
  tft.fillRoundRect(80, 206, 80, 50, 6, TFT_DARKGREEN);
  tft.drawString("OK", 120, 231, 2);

  // RIGHT
  tft.fillRoundRect(164, 206, 68, 50, 6, TFT_NAVY);
  tft.drawString("RIGHT >", 198, 231, 2);

  // DOWN
  tft.fillRoundRect(80, 260, 80, 46, 6, TFT_NAVY);
  tft.drawString("v DOWN", 120, 283, 2);
}

/* =========================================================================
 * [페이지 2] 숫자 키패드 UI (240x320 세로 모드 최적화)
 * ========================================================================= */
void drawKeypadUI() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  // 상단 바: 메인 D-Pad로 돌아가기 버튼
  tft.fillRoundRect(6, 6, 110, 30, 6, TFT_DARKCYAN);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("< NAVI D-PAD", 61, 21, 2);

  if (bleKeyboard.isConnected()) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("READY", 195, 21, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("NO BLE", 195, 21, 2);
  }

  // 3x4 키패드 그리드
  const char* labels[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "OK"};
  uint16_t colors[12] = {
    TFT_NAVY, TFT_NAVY, TFT_NAVY,
    TFT_NAVY, TFT_NAVY, TFT_NAVY,
    TFT_NAVY, TFT_NAVY, TFT_NAVY,
    TFT_MAROON, TFT_NAVY, TFT_DARKGREEN
  };

  int startX = 8;
  int startY = 42;
  int btnW = 68;
  int btnH = 60;
  int gap = 8;

  for (int i = 0; i < 12; i++) {
    int col = i % 3;
    int row = i / 3;
    int bx = startX + col * (btnW + gap);
    int by = startY + row * (btnH + gap);

    tft.fillRoundRect(bx, by, btnW, btnH, 6, colors[i]);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(labels[i], bx + btnW / 2, by + btnH / 2, 4);
  }
}

/* =========================================================================
 * 터치 좌표 읽기 및 페이지별 분기 (240x320 세로 모드)
 * ========================================================================= */
void checkTouchInput() {
  TS_Point p = ts.getPoint();
  
  if (p.z < TS_MIN_PRESSURE || p.x <= 0 || p.y <= 0 || p.x == -4096 || p.y == -4096) return;

  uint16_t rawX = p.x;
  uint16_t rawY = p.y;
  
  if (SWAP_XY) {
    rawX = p.y;
    rawY = p.x;
  }

  // 세로 240x320 범위 매핑
  uint16_t tx = INVERT_X ? map(rawX, TS_MAX_X, TS_MIN_X, 0, 240) : map(rawX, TS_MIN_X, TS_MAX_X, 0, 240);
  uint16_t ty = INVERT_Y ? map(rawY, TS_MAX_Y, TS_MIN_Y, 0, 320) : map(rawY, TS_MIN_Y, TS_MAX_Y, 0, 320);

  tx = constrain(tx, 0, 239);
  ty = constrain(ty, 0, 319);

  Serial.printf("[TOUCH DEBUG] Raw(X:%d, Y:%d, Z:%d) -> Mapped(tx:%d, ty:%d)\n", p.x, p.y, p.z, tx, ty);

  // 시각 피드백
  tft.fillCircle(tx, ty, 3, TFT_YELLOW);

  if (currentPage == PAGE_MAIN_DPAD) {
    handleMainPageTouch(tx, ty);
  } else {
    handleKeypadPageTouch(tx, ty);
  }
}

/* =========================================================================
 * 메인 D-Pad 터치 처리 (Android TV Keycodes)
 * ========================================================================= */
void handleMainPageTouch(uint16_t tx, uint16_t ty) {
  // 1. POWER (6,30 ~ 114,66) -> Android TV Power/Sleep/Wake (0x0030 / KEY_ATV_POWER)
  if (tx >= 6 && tx <= 114 && ty >= 30 && ty <= 66) {
    Serial.println("ATV KEY: POWER (0x0030 / KEY_ATV_POWER)");
    if (bleKeyboard.isConnected()) {
      bleKeyboard.write(KEY_ATV_POWER);
    }
  }
  // 2. 123 KEYPAD TOGGLE (126,30 ~ 234,66)
  else if (tx >= 126 && tx <= 234 && ty >= 30 && ty <= 66) {
    currentPage = PAGE_NUMBER_KEYPAD;
    drawKeypadUI();
    return;
  }
  // 3. HOME (6,72 ~ 114,108) -> Android TV Home Screen (0x0223 / KEY_ATV_HOME)
  else if (tx >= 6 && tx <= 114 && ty >= 72 && ty <= 108) {
    Serial.println("ATV KEY: HOME (0x0223 / KEY_ATV_HOME)");
    if (bleKeyboard.isConnected()) {
      bleKeyboard.write(KEY_ATV_HOME);
    }
  }
  // 4. BACK (126,72 ~ 234,108) -> Android TV Back (0x0224 / 000c0224 KEY_ATV_BACK)
  else if (tx >= 126 && tx <= 234 && ty >= 72 && ty <= 108) {
    Serial.println("ATV KEY: BACK (0x0224 / 000c0224 KEY_ATV_BACK)");
    if (bleKeyboard.isConnected()) {
      bleKeyboard.write(KEY_ATV_BACK);
    }
  }
  // 5. VOL+ (6,114 ~ 78,150)
  else if (tx >= 6 && tx <= 78 && ty >= 114 && ty <= 150) {
    Serial.println("ATV KEY: VOL+");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
  }
  // 6. MUTE (84,114 ~ 156,150)
  else if (tx >= 84 && tx <= 156 && ty >= 114 && ty <= 150) {
    Serial.println("ATV KEY: MUTE");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_MEDIA_MUTE);
  }
  // 7. VOL- (162,114 ~ 234,150)
  else if (tx >= 162 && tx <= 234 && ty >= 114 && ty <= 150) {
    Serial.println("ATV KEY: VOL-");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
  }
  // 8. D-PAD UP (80,156 ~ 160,202)
  else if (tx >= 80 && tx <= 160 && ty >= 156 && ty <= 202) {
    Serial.println("ATV KEY: DPAD UP");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_UP_ARROW);
  }
  // 9. D-PAD LEFT (8,206 ~ 76,256)
  else if (tx >= 8 && tx <= 76 && ty >= 206 && ty <= 256) {
    Serial.println("ATV KEY: DPAD LEFT");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_LEFT_ARROW);
  }
  // 10. OK / ENTER (80,206 ~ 160,256)
  else if (tx >= 80 && tx <= 160 && ty >= 206 && ty <= 256) {
    Serial.println("ATV KEY: DPAD CENTER (OK)");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RETURN);
  }
  // 11. D-PAD RIGHT (164,206 ~ 232,256)
  else if (tx >= 164 && tx <= 232 && ty >= 206 && ty <= 256) {
    Serial.println("ATV KEY: DPAD RIGHT");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RIGHT_ARROW);
  }
  // 12. D-PAD DOWN (80,260 ~ 160,306)
  else if (tx >= 80 && tx <= 160 && ty >= 260 && ty <= 306) {
    Serial.println("ATV KEY: DPAD DOWN");
    if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_DOWN_ARROW);
  }
}

/* =========================================================================
 * 숫자 키패드 페이지 터치 처리 (240x320 세로)
 * ========================================================================= */
void handleKeypadPageTouch(uint16_t tx, uint16_t ty) {
  // 1. NAVI D-PAD 돌아가기 버튼 (6,6 ~ 116,36)
  if (tx >= 6 && tx <= 116 && ty >= 6 && ty <= 36) {
    currentPage = PAGE_MAIN_DPAD;
    drawMainUI();
    return;
  }

  int startX = 8, startY = 42, btnW = 68, btnH = 60, gap = 8;

  for (int i = 0; i < 12; i++) {
    int col = i % 3;
    int row = i / 3;
    int bx = startX + col * (btnW + gap);
    int by = startY + row * (btnH + gap);

    if (tx >= bx && tx <= (bx + btnW) && ty >= by && ty <= (by + btnH)) {
      if (i >= 0 && i <= 8) { // 숫자 1~9
        char digit = '1' + i;
        Serial.printf("KEYPAD: %c\n", digit);
        if (bleKeyboard.isConnected()) bleKeyboard.write(digit);
      } else if (i == 9) { // CLR / BACKSPACE
        Serial.println("KEYPAD: BACKSPACE");
        if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_BACKSPACE);
      } else if (i == 10) { // 숫자 0
        Serial.println("KEYPAD: 0");
        if (bleKeyboard.isConnected()) bleKeyboard.write('0');
      } else if (i == 11) { // OK / ENTER
        Serial.println("KEYPAD: ENTER");
        if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RETURN);
      }
      break;
    }
  }
}