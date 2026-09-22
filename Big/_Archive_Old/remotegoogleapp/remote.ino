#include <BleKeyboard.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// 블루투스 키보드 객체 생성 (안드로이드에 표시될 이름)
BleKeyboard bleKeyboard("ESP32 Smart Remote", "CustomMaker", 100);

TFT_eSPI tft = TFT_eSPI();

// 화면 해상도 설정 (사용하시는 패널 규격에 맞게 조정, 예: 320x240 또는 480x320)
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

void setup() {
  Serial.begin(115200);
  
  // 디스플레이 및 터치 초기화
  tft.init();
  tft.setRotation(1); // 가로 모드
  tft.fillScreen(TFT_BLACK);
  
  // 블루투스 HID 시작
  bleKeyboard.begin();
  
  drawRemoteUI();
}

void loop() {
  // 블루투스가 연결되어 있을 때만 터치 입력 처리
  if (bleKeyboard.isConnected()) {
    checkTouchInput();
  } else {
    // 연결 끊김 표시 등 처리 가능
  }
}

// 리모컨 UI 화면 그리기
void drawRemoteUI() {
  tft.fillScreen(TFT_DARKGREY);
  
  // 상단 바 (전원, 볼륨, 홈/백)
  tft.fillRect(10, 10, 60, 40, TFT_RED);     // 전원 버튼
  tft.drawString("POWER", 20, 22);

  tft.fillRect(90, 10, 60, 40, TFT_BLUE);    // 홈 (HOME)
  tft.drawString("HOME", 100, 22);

  tft.fillRect(170, 10, 60, 40, TFT_BLUE);   // 뒤로가기 (BACK)
  tft.drawString("BACK", 180, 22);

  // 중앙 방향키 영역 (십자 패드 스타일)
  // [위]
  tft.fillRect(130, 70, 60, 50, TFT_NAVY);
  tft.drawString("^", 155, 88);
  // [좌]
  tft.fillRect(70, 130, 50, 60, TFT_NAVY);
  tft.drawString("<", 90, 150);
  // [확인/OK]
  tft.fillRect(130, 130, 60, 60, TFT_DARKGREEN);
  tft.drawString("OK", 150, 150);
  // [우]
  tft.fillRect(200, 130, 50, 60, TFT_NAVY);
  tft.drawString(">", 220, 150);
  // [아래]
  tft.fillRect(130, 200, 60, 50, TFT_NAVY);
  tft.drawString("v", 155, 215);

  // 우측 볼륨 및 숫자 키패드 진입 버튼 등 배치 공간
}

// 터치 좌표를 읽고 매칭되는 키를 블루투스로 전송하는 함수
void checkTouchInput() {
  // 여기에 디스플레이 터치 좌표 읽는 코드 추가 (예: ts.getTouch(&x, &y))
  // 터치된 좌표(x, y)에 따라 아래와 같이 키를 쏩니다.
  
  /* 예시 로직:
  if (touch_power) {
     bleKeyboard.write(KEY_MEDIA_MUTE); // 또는 파워 키
  }
  else if (touch_ok) {
     bleKeyboard.write(KEY_RETURN); // DPAD Center
  }
  else if (touch_home) {
     bleKeyboard.write(KEY_MEDIA_HOME); // 안드로이드 홈
  }
  else if (touch_back) {
     bleKeyboard.write(KEY_ESC); // 뒤로가기
  }
  */
}