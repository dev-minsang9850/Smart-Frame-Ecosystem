# 준비물
## 1. 미니 전자 액자 만들기 준비사항

- **ESP32-2432S028R 스마트 디스플레이 보드** 1개
- **Micro SD 카드 (32GB 이하, FAT32로 포맷된 것)** 1개
- **C타입 USB 데이터 케이블** 1개 (컴퓨터와 보드 연결용) 혹은 5핀 케이블
  5V 1A 혹은 5V 2A 충전선으로 해야하며, 고속 충전선을 사용 시 고장날 수 있음.
- **실습용 사진 파일 (`photo.jpg`)** * _크기가 가로 320, 세로 240 픽셀로 맞춰진 JPG 파일이 SD 카드 안에 들어있어야 합니다._
    

## 2. 컴퓨터 환경 설정 및 세팅 (사전 작업)

이 부분은 교장선생님들이 직접 하기에 복잡할 수 있으므로, 강사님이 미리 세팅해 두거나 가이드에 따라 천천히 진행하도록 안내해 주세요.

### ① 아두이노 IDE 설치 및 보드 추가

1. 컴퓨터에 **아두이노 IDE(Arduino IDE)** 프로그램을 설치하고 실행합니다.
    
2. 파일 ➔ 기본 설정(Preferences)의 `추가 보드 매니저 URL`에 아래 주소를 넣습니다.
    
    > `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
!! 만일 설치가 안되는 경우 아래 주소를 참고해주세요.
    > `https://cdn.jsdelivr.net/gh/espressif/arduino-esp32@gh-pages/package_esp32_index.json //보드 매니저 설치 실패 경우`
    
3. 왼쪽 메뉴의 **보드 매니저**에서 **esp32**를 검색하여 설치합니다.
    
4. 툴 ➔ 보드에서 "ESP32 Dev Module"을 선택합니다.
    

### ② 필수 라이브러리 설치

아두이노 IDE 왼쪽 메뉴의 라이브러리 관리자(책꽂이 모양)에서 아래 두 가지를 검색하여 설치합니다.

1. `TFT_eSPI` (화면 제어용)
2. `TJpg_Decoder` (JPG 사진 압축 해제용)
3. `ArduinoJson` (사진 데이터 전송)
4. `XPT2046_Touchscreen`(디스플레이의 터치스크린 제어)

### ③ User_Setup.h 파일 수정(사진 업로드 시 색상 이상현상 제거)
``` h
#define USER_SETUP_LOADED

// 디스플레이 칩셋 드라이버 설정
#define ILI9341_DRIVER

// ⭐ [중요] 색상 표현 방식 정의
// ILI9341 칩셋 중 일부는 RGB 순서가 아닌 BGR 순서를 사용합니다.
// 이 코드를 추가하여 색상을 정상적으로 돌립니다.
#define TFT_RGB_ORDER TFT_BGR

// CYD 보드 전용 핀 맵 설정
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21  // 백라이트 핀

// 폰트 및 기타 설정 (필요 시 수정)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  55000000
#define SPI_READ_FREQUENCY  20000000
```


## 3. 전자액자 사진 출력 소스코드(단일 사진)

``` c++
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

TFT_eSPI tft = TFT_eSPI();

// SD 카드에 저장한 사진 파일 이름 (맥북 미리보기로 만든 320x240 JPG)
const char* imageName = "파일이름.jpg";

// TJpg_Decoder가 이미지를 한 픽셀씩 화면에 그려주는 콜백 함수
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if ( y >= tft.height() ) return false;
  
  for (int i = 0; i < w * h; i++) {
    bitmap[i] = (bitmap[i] >> 8) | (bitmap[i] << 8);
  }

  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

void setup() {
  Serial.begin(115200);

  // 1. CYD 보드 백라이트 켜기 (21번 핀)
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH); 

  // 2. 디스플레이 기본 설정 (가로 모드)
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);

  // 3. SD 카드 연결 (CYD 보드는 5번 핀이 CS)
  if (!SD.begin(5)) {
    Serial.println("SD Card Mount Failed!");
    tft.setTextColor(TFT_RED);
    tft.drawString("SD Card Error!", 10, 10, 4);
    while (1) delay(1000);
  }
  Serial.println("SD Card Initialized.");

  // 4. JPG 디코더 등록
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);

  // 5. 사진을 화면에 출력
  Serial.print("Loading image: "); Serial.println(imageName);
  TJpgDec.drawSdJpg(0, 0, imageName);
  Serial.println("Image displayed successfully.");
}

void loop() {
  // 고정 화면이므로 대기합니다.
  delay(1000);
}
```

## 4. 전자 액자 사진 출력 소스코드(2개 이상)
``` c++
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#define SD_CS_PIN 5

TFT_eSPI tft = TFT_eSPI();

// ⭐ [슬라이드쇼 사진 목록] SD 카드에 저장된 3개의 사진 파일 이름
const char* photos[] = {
	"/photo.jpg",
	"/photo1.jpg",
	"/photo2.jpg",
	"/photo3.jpg"
};
// 전체 사진 개수(전체 - 1)
const int totalPhotos = 3;

// 현재 출력 중인 사진의 번호 (0번부터 시작)
int currentIndex = 0;

// 이미지를 화면에 한 픽셀씩 그려주는 함수 (색상 왜곡 교정 포함)
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
	if ( y >= tft.height() ) return false;
	for (int i = 0; i < w * h; i++) {
		bitmap[i] = (bitmap[i] >> 8) | (bitmap[i] << 8);
	}
	tft.pushImage(x, y, w, h, bitmap);
	return true;
}

void setup() {
	Serial.begin(115200);
	delay(500); // 전원 및 하드웨어 안정화 대기
	
	// 백화 현상 방지를 위한 디스플레이 선(先) 초기화
	pinMode(21, OUTPUT);
	digitalWrite(21, LOW);
	
	tft.init();
	tft.setRotation(3); // 180도 회전된 가로 모드
	tft.fillScreen(TFT_BLACK);
	digitalWrite(21, HIGH); // 화면 준비 완료 후 백라이트 켜기
	delay(200);
	
	// 디스플레이 준비 후 SD 카드 초기화 (SPI 버스 충돌 방지)
	pinMode(SD_CS_PIN, OUTPUT);
	digitalWrite(SD_CS_PIN, HIGH);
	delay(100);

	if (!SD.begin(SD_CS_PIN)) {
		
		Serial.println("SD 카드 연결 실패!");
		
		tft.setTextColor(TFT_RED);
		
		tft.drawString("SD Card Error!", 10, 10, 4);
		while (1) delay(1000);
	}
	
	Serial.println("SD 카드 연결 성공! 슬라이드쇼를 시작합니다.");
	
	// JPG 디코더 설정
	TJpgDec.setJpgScale(1);
	TJpgDec.setCallback(tft_output);
}

  

// ⭐ [핵심 반복 영역] 전원이 켜져 있는 동안 계속 실행됩니다.

void loop() {
	Serial.print("사진 출력 중: ");
	Serial.println(photos[currentIndex]);
	
	// 잔상이나 깨짐 현상을 막기 위해 사진을 그리기 직전 화면을 검게 비웁니다.
	tft.fillScreen(TFT_BLACK);
	
	// 현재 인덱스 번호에 해당하는 사진을 SD카드에서 읽어와 화면에 그립니다.
	TJpgDec.drawSdJpg(0, 0, photos[currentIndex]);
	
	// 다음 사진으로 순서를 넘깁니다. (2번 사진 이후엔 다시 0번으로 순환)
	currentIndex = (currentIndex + 1) % totalPhotos;
	
	// 10초(10,000 밀리초) 동안 현재 화면을 유지하면서 대기합니다.
	delay(50000);
}

```


## 5. 최종 보드 전송 방법 안내

1. 보드와 컴퓨터를 C타입 케이블로 연결합니다.
    
2. 아두이노 IDE 상단 메뉴 "tool➔ ports"에서 새로 연결된 포트(예: `/dev/cu.usbserial-xxx`)를 선택합니다.
    
3. tool ➔ Upload Speed를 `115200`으로 낮추어 설정합니다. (안정적인 전송을 위해 중요)
    
4. 왼쪽 상단의 **화살표 모양 업로드(Upload) 버튼을 누르고 화면 하단에 "Done uploading"이 뜰 때까지 기다립니다.
