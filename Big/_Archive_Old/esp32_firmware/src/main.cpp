#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <XPT2046_Touchscreen.h>

// BLE UUIDs for Provisioning
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Hardware Pins for ESP32 CYD (ESP32-2432S028R)
#define SD_CS_PIN           5
#define TFT_CS_PIN          15
#define TFT_BL_PIN          21

// XPT2046 Touch Controller Dedicated Pins (Physical VSPI Routing on CYD)
#define XPT2046_MOSI        32
#define XPT2046_MISO        39
#define XPT2046_CLK         25
#define XPT2046_CS          33
#define XPT2046_IRQ         36

// Constants & Globals
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI); // 터치 전용 독립 SPI 채널 (HSPI 사용으로 VSPI 충돌 완벽 방지)
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ); // CS(33)와 IRQ(36) 사용
WebServer server(80);
Preferences preferences;

// Provisioning state
bool inProvisioningMode = false;
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;

// Photo slideshow state
std::vector<String> photoFiles;
int currentPhotoIndex = -1;
unsigned long lastPhotoSwitchTime = 0;
const unsigned long slideshowInterval = 10000; // 10 seconds
bool isShowingPhoto = false;
bool isUploading = false; // 업로드 중 SPI 충돌 방지 플래그

// 터치 스캔 주기 제어 변수 (SPI 버스 병목 현상 방지)
unsigned long lastTouchCheckTime = 0;
const unsigned long touchCheckInterval = 100; // 100ms (0.1초) 주기로만 터치 체크

// DEL 버튼 표시 및 제스처 상태 변수 (더블탭 / 길게 누르기)
bool isDelVisible = false;
unsigned long delVisibleStartTime = 0;
unsigned long touchStartTime = 0;
bool isTouching = false;
unsigned long lastTapReleaseTime = 0;

// Delete UI Area coordinates (Rotation 3: 320x240)
const int TRASH_X = 270;
const int TRASH_Y = 190;
const int TRASH_W = 50;
const int TRASH_H = 50;

File uploadFile;
bool uploadFailed = false;

// Serial Upload State
File serialUploadFile;
bool serialIsUploading = false;
uint32_t serialExpectedBytes = 0;
uint32_t serialReceivedBytes = 0;

// Function declarations
void startBLEProvisioning();
void connectToWiFi();
void setupWebServer();
void loadPhotoList();
void showNextPhoto();
void showPhoto(const String& path);
void drawTrashIcon();
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void deleteCurrentPhoto();
void handleTouch();

// BLE Callback for receiving credentials
class ProvisioningCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        String value = String(pCharacteristic->getValue().c_str());
        if (value.length() > 0) {
            String data = value;
            Serial.printf("BLE Received: %s\n", data.c_str());
            
            // Expected format: "SSID;Password"
            int splitIdx = data.indexOf(';');
            if (splitIdx != -1) {
                String ssid = data.substring(0, splitIdx);
                String password = data.substring(splitIdx + 1);
                
                Serial.printf("Saving SSID: %s, PW: %s\n", ssid.c_str(), password.c_str());
                
                preferences.begin("wifi-creds", false);
                preferences.putString("ssid", ssid);
                preferences.putString("password", password);
                preferences.end();
                
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_GREEN);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("Credentials Received!", 160, 100, 4);
                tft.drawString("Rebooting...", 160, 140, 2);
                delay(2000);
                
                ESP.restart();
            } else {
                Serial.println("Invalid format. Use 'SSID;Password'");
            }
        }
    }
};

#define BOOT_BUTTON_PIN     0

void setup() {
    Serial.begin(115200);
    
    // BOOT 버튼 핀 풀업 설정
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    // Configure chip select pins as output and set high to disable SPI conflict initially
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(XPT2046_CS, OUTPUT);
    digitalWrite(XPT2046_CS, HIGH);
    
    // Explicitly turn on the TFT Backlight for Arduino IDE build
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(3); // Landscape: 320x240
    tft.setSwapBytes(true); // RGB 색상 꼬임 방지 바이트 스왑 활성화 (화질/색감 정상화)
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Initializing...", 160, 120, 4);

    // 3초간 진행바를 그리며 BOOT 버튼(GPIO 0) 감지
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Reset Option", 160, 60, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Hold BOOT button to Reset WiFi", 160, 110, 2);

    bool forceReset = false;
    unsigned long bootStartTime = millis();
    while (millis() - bootStartTime < 3000) {
        int progress = map(millis() - bootStartTime, 0, 3000, 0, 100);
        tft.fillRect(60, 160, 200, 10, TFT_DARKGREY);
        tft.fillRect(60, 160, progress * 2, 10, TFT_BLUE);
        
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            delay(150); // 디바운스 대기
            if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                forceReset = true;
                break;
            }
        }
        delay(50);
    }

    if (forceReset) {
        Serial.println("Factory Reset confirmed via physical BOOT button!");
        preferences.begin("wifi-creds", false);
        preferences.clear();
        preferences.end();
        
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("WiFi Cleared!", 160, 100, 4);
        tft.drawString("Entering BLE setup...", 160, 140, 2);
        delay(2000);
    }

    // 강제로 TFT와 터치 CS를 한 번 더 끊어줌 (통신 간섭 가드)
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(XPT2046_CS, HIGH);
    delay(50);

    // SD 카드 마운트 시도 (TFT_eSPI가 이미 초기화한 전역 SPI 객체를 그대로 재사용)
    if (!SD.begin(SD_CS_PIN, SPI, 4000000)) {
        Serial.println("SD Card mount failed!");
        tft.fillScreen(TFT_RED);
        tft.drawString("SD Card Error!", 160, 120, 4);
        while (true) delay(1000);
    }
    Serial.println("SD Card mounted successfully.");

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_output);

    preferences.begin("wifi-creds", true);
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    preferences.end();

    if (ssid == "" || ssid.length() == 0) {
        Serial.println("No Wi-Fi credentials found. Starting BLE Provisioning.");
        startBLEProvisioning();
    } else {
        Serial.printf("Stored SSID found: %s. Attempting connection.\n", ssid.c_str());
        connectToWiFi();
    }

    loadPhotoList();
    lastPhotoSwitchTime = millis();

    // Initialize XPT2046 Touch Screen with dedicated SPI pins (HSPI)
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(3); // Match screen rotation
}

void loop() {
    if (inProvisioningMode) {
        delay(100);
        return;
    }

    server.handleClient();
    handleSerialUpload(); // PC 시리얼 통신 감지

    // 업로드 중이 아닐 때만 화면 갱신 및 터치 처리 (SPI 버스 충돌 100% 방지)
    if (!isUploading) {
        // DEL 버튼 5초 후 자동 숨김 처리
        if (isDelVisible && (millis() - delVisibleStartTime >= 5000)) {
            isDelVisible = false;
            if (isShowingPhoto && photoFiles.size() > 0) {
                showPhoto(photoFiles[currentPhotoIndex]);
            }
        }

        // 0.1초(100ms) 간격으로만 터치 입력 체크
        if (millis() - lastTouchCheckTime >= touchCheckInterval) {
            handleTouch();
            lastTouchCheckTime = millis();
        }

        if (photoFiles.size() > 0) {
            if (photoFiles.size() == 1) {
                // 사진이 오직 1장뿐이라면, 한 번만 화면에 그리고 가만히 대기 (깜빡임 방지)
                if (!isShowingPhoto) {
                    showPhoto(photoFiles[currentPhotoIndex]);
                    isShowingPhoto = true;
                }
            } else {
                // 사진이 2장 이상일 때만 10초 주기로 슬라이드쇼 작동
                if (millis() - lastPhotoSwitchTime >= slideshowInterval || !isShowingPhoto) {
                    showNextPhoto();
                    lastPhotoSwitchTime = millis();
                }
            }
        } else {
            if (!isShowingPhoto) {
                // 이전 잔상(DEL 버튼 등)이 남지 않도록 화면 전체를 검은색으로 완전히 청소!
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("No Photos found on SD Card.", 160, 80, 2);
                tft.drawString("Upload photos via App.", 160, 110, 2);
                tft.setTextColor(TFT_GREEN);
                tft.drawString(WiFi.localIP().toString(), 160, 145, 2);
                
                // REFRESH 파란색 둥근 버튼 렌더링
                tft.fillRoundRect(100, 185, 120, 35, 6, TFT_BLUE);
                tft.drawRoundRect(100, 185, 120, 35, 6, TFT_WHITE);
                tft.setTextColor(TFT_WHITE);
                tft.drawString("REFRESH", 160, 202, 2);
                
                isShowingPhoto = true;
            }
        }
    }
}

// BLE Provisioning
void startBLEProvisioning() {
    inProvisioningMode = true;
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_BLUE);
    tft.drawString("BLE Provisioning Mode", 160, 60, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Device: ESP32-SmartFrame", 160, 110, 2);
    tft.drawString("Send WiFi credentials from App", 160, 140, 2);

    BLEDevice::init("ESP32-SmartFrame");
    pServer = BLEDevice::createServer();
    
    BLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    
    pCharacteristic->setCallbacks(new ProvisioningCallbacks());
    pService->start();
    
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE Advertising started. Waiting for connection...");
}

// Wi-Fi Connection
void connectToWiFi() {
    preferences.begin("wifi-creds", true);
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    preferences.end();

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Connecting to WiFi...", 160, 80, 4);
    tft.drawString(ssid, 160, 130, 2);

    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 30) {
        delay(1000);
        attempt++;
        tft.drawString(".", 100 + (attempt * 6), 160, 2);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP address: %s\n", WiFi.localIP().toString().c_str());
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("WiFi Connected!", 160, 80, 4);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(WiFi.localIP().toString(), 160, 130, 4);
        delay(3000);
        
        setupWebServer();
    } else {
        Serial.println("Connection failed. Retrying later (credentials kept). Falling back to BLE.");
        tft.fillScreen(TFT_RED);
        tft.drawString("Connection Failed", 160, 100, 4);
        tft.drawString("Starting BLE Config...", 160, 140, 2);
        delay(2000);
        
        startBLEProvisioning();
    }
}

// HTTP Web Server Endpoints
void handleUploadFile() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        isUploading = true; // 업로드 시작: 슬라이드쇼 중지
        String filename = upload.filename;
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        
        Serial.printf("Upload start. File: %s\n", filename.c_str());
        
        if (SD.exists(filename)) {
            SD.remove(filename);
        }
        
        uploadFailed = false;
        uploadFile = SD.open(filename, FILE_WRITE);
        
        if (!uploadFile) {
            Serial.printf("CRITICAL ERROR: Failed to open file %s for writing!\n", filename.c_str());
            uploadFailed = true;
            isUploading = false;
            
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("SD WRITE ERROR!", 160, 100, 4);
            tft.drawString("Check SD Card", 160, 140, 2);
            
            isShowingPhoto = false; 
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile && !uploadFailed) {
            size_t written = uploadFile.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                Serial.println("WRITE FAIL: Size mismatch!");
                uploadFailed = true;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            if (uploadFailed) {
                Serial.println("Upload discarded due to SD errors.");
            } else {
                Serial.printf("Upload complete. Size: %u bytes\n", upload.totalSize);
            }
        }
        isUploading = false; // 업로드 종료: 슬라이드쇼 재개
    }
}

void handleUploadFinish() {
    server.sendHeader("Connection", "close");
    isUploading = false; // 확실히 초기화
    if (uploadFailed) {
        server.send(500, "text/plain", "SD Card Write Error");
    } else {
        server.send(200, "text/plain", "Upload success");
        loadPhotoList();
    }
}

void handleListPhotos() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    File dir = SD.open("/");
    if (dir) {
        File file = dir.openNextFile();
        while (file) {
            String name = String(file.name());
            if (name.lastIndexOf('/') != -1) {
                name = name.substring(name.lastIndexOf('/') + 1);
            }
            if (name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".JPG")) {
                arr.add(name);
            }
            file = dir.openNextFile();
        }
        dir.close();
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleDeletePhoto() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }
    
    String filename = server.arg("file");
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }
    
    if (SD.exists(filename)) {
        if (SD.remove(filename)) {
            Serial.printf("Deleted remote file: %s\n", filename.c_str());
            server.send(200, "text/plain", "File deleted successfully");
            loadPhotoList();
            
            if (currentPhotoIndex >= 0 && currentPhotoIndex < photoFiles.size()) {
                if (photoFiles[currentPhotoIndex] == filename) {
                    isShowingPhoto = false;
                }
            }
        } else {
            server.send(500, "text/plain", "Failed to delete file");
        }
    } else {
        server.send(404, "text/plain", "File not found");
    }
}

void handleViewPhoto() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }
    
    String filename = server.arg("file");
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }
    
    if (SD.exists(filename)) {
        File file = SD.open(filename, FILE_READ);
        server.streamFile(file, "image/jpeg");
        file.close();
    } else {
        server.send(404, "text/plain", "File not found");
    }
}

void setupWebServer() {
    server.on("/upload", HTTP_POST, handleUploadFinish, handleUploadFile);
    server.on("/list", HTTP_GET, handleListPhotos);
    server.on("/delete", HTTP_DELETE, handleDeletePhoto);
    server.on("/view", HTTP_GET, handleViewPhoto);
    server.on("/delete_get", HTTP_GET, handleDeletePhoto);
    
    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE");
            server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            server.send(204);
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    server.enableCORS(true); // Allow Web App to connect via browser
    server.begin();
    Serial.println("HTTP Web Server started on port 80 (CORS Enabled).");
}

// PC Serial Sync Logic
void handleSerialUpload() {
    if (!serialIsUploading) {
        if (Serial.available() > 0) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            if (cmd.startsWith("CMD:UPLOAD_START|")) {
                int firstPipe = cmd.indexOf('|');
                int secondPipe = cmd.indexOf('|', firstPipe + 1);
                if (firstPipe != -1 && secondPipe != -1) {
                    String filename = cmd.substring(firstPipe + 1, secondPipe);
                    if (!filename.startsWith("/")) filename = "/" + filename;
                    
                    String sizeStr = cmd.substring(secondPipe + 1);
                    serialExpectedBytes = sizeStr.toInt();
                    serialReceivedBytes = 0;
                    
                    if (SD.exists(filename)) SD.remove(filename);
                    serialUploadFile = SD.open(filename, FILE_WRITE);
                    
                    if (serialUploadFile) {
                        serialIsUploading = true;
                        isUploading = true; // Pause slideshow
                        
                        tft.fillScreen(TFT_BLACK);
                        tft.setTextColor(TFT_YELLOW);
                        tft.setTextDatum(MC_DATUM);
                        tft.drawString("Receiving from PC...", 160, 100, 4);
                        tft.drawString(filename, 160, 130, 2);
                        
                        Serial.println("ACK:READY");
                    } else {
                        Serial.println("ERR:SD_OPEN_FAIL");
                    }
                } else {
                    Serial.println("ERR:INVALID_CMD");
                }
            }
        }
    } else {
        // Reading binary chunks
        if (Serial.available() > 0) {
            uint8_t buf[256];
            int toRead = Serial.available();
            if (toRead > sizeof(buf)) toRead = sizeof(buf);
            
            uint32_t remaining = serialExpectedBytes - serialReceivedBytes;
            if (toRead > remaining) toRead = remaining;
            
            int readBytes = Serial.readBytes(buf, toRead);
            if (readBytes > 0) {
                serialUploadFile.write(buf, readBytes);
                serialReceivedBytes += readBytes;
                
                // Reply to PC so it sends the next chunk
                Serial.println("ACK:CHUNK");
                
                // Draw Progress Bar
                int progress = map(serialReceivedBytes, 0, serialExpectedBytes, 0, 200);
                tft.fillRect(60, 160, 200, 10, TFT_DARKGREY);
                tft.fillRect(60, 160, progress, 10, TFT_GREEN);
                
                if (serialReceivedBytes >= serialExpectedBytes) {
                    serialUploadFile.close();
                    serialIsUploading = false;
                    isUploading = false;
                    Serial.println("ACK:SUCCESS");
                    
                    tft.fillScreen(TFT_BLACK);
                    tft.setTextColor(TFT_GREEN);
                    tft.drawString("PC Upload Complete!", 160, 120, 4);
                    delay(1500);
                    
                    loadPhotoList(); // Refresh slideshow with new photo
                }
            }
        }
    }
}

// Slideshow & JPG Decoding (TJpgDec)
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return false;
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}

// SD 카드 루트 디렉토리 스캔 방식으로 교정 (isDirectory 오작동 필터 제거)
void loadPhotoList() {
    photoFiles.clear();
    
    File dir = SD.open("/");
    if (!dir) {
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        String name = String(file.name());
        if (!name.startsWith("/")) {
            name = "/" + name;
        }
        
        Serial.printf("Found file on SD: %s\n", name.c_str());
        
        // 맥북(macOS)에서 생성되는 숨김 파일(._ 로 시작하는 파일) 무시
        if (name.indexOf("._") != -1) {
            file = dir.openNextFile();
            continue;
        }
        
        if (name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".JPG")) {
            photoFiles.push_back(name);
        }
        file = dir.openNextFile();
    }
    dir.close();
    
    Serial.printf("Total JPGs loaded: %d\n", photoFiles.size());
    
    // 인덱스 범위 초과 및 초기 상태(-1) 교정
    if (currentPhotoIndex < 0 || currentPhotoIndex >= (int)photoFiles.size()) {
        currentPhotoIndex = 0;
    }

    isShowingPhoto = false;
}

void showNextPhoto() {
    if (photoFiles.size() == 0) {
        isShowingPhoto = false;
        return;
    }
    
    currentPhotoIndex = (currentPhotoIndex + 1) % photoFiles.size();
    showPhoto(photoFiles[currentPhotoIndex]);
}

void showPhoto(const String& path) {
    Serial.printf("Displaying: %s\n", path.c_str());
    
    tft.fillScreen(TFT_BLACK);
    
    // TJpgDec 내부에서 SD 카드를 열어 읽음
    TJpgDec.drawSdJpg(0, 0, path.c_str());
    
    isDelVisible = false; // 사진이 표시될 때는 DEL 버튼 숨김
    isShowingPhoto = true;
}

void drawTrashIcon() {
    tft.fillRoundRect(TRASH_X, TRASH_Y, TRASH_W, TRASH_H, 8, TFT_RED);
    tft.drawRoundRect(TRASH_X, TRASH_Y, TRASH_W, TRASH_H, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("DEL", TRASH_X + (TRASH_W / 2), TRASH_Y + (TRASH_H / 2), 2);
}

// Touch Input Management
void handleTouch() {
    bool touchedNow = ts.touched();
    
    if (touchedNow) {
        TS_Point p = ts.getPoint();
        
        // 2. Bypass invalid/error raw signals
        if (p.x == -4096 || p.y == -4096 || p.x <= 0 || p.y <= 0) {
            return;
        }
        
        // Swap X and Y mapping to align with rotated CYD hardware touch sensors
        uint16_t tx = map(p.y, 240, 3800, 0, 320); 
        uint16_t ty = map(p.x, 240, 3800, 0, 240); 
        
        Serial.printf("Touch raw: (%d, %d) -> Mapped: (%d, %d)\n", p.x, p.y, tx, ty);
        
        // 터치 시작 및 제스처 (더블 탭 / 길게 누르기) 감지
        if (!isTouching) {
            isTouching = true;
            touchStartTime = millis();
            
            // 더블 탭 감지 (350ms 이내 재터치)
            if (millis() - lastTapReleaseTime < 350) {
                if (isShowingPhoto && photoFiles.size() > 0 && !isDelVisible) {
                    isDelVisible = true;
                    delVisibleStartTime = millis();
                    drawTrashIcon();
                    Serial.println("Double tap detected -> DEL button shown");
                    return;
                }
            }
        } else {
            // 길게 누르기 감지 (400ms 이상 누르고 있음)
            if (millis() - touchStartTime > 400) {
                if (isShowingPhoto && photoFiles.size() > 0 && !isDelVisible) {
                    isDelVisible = true;
                    delVisibleStartTime = millis();
                    drawTrashIcon();
                    Serial.println("Long press detected -> DEL button shown");
                    return;
                }
            }
        }
        
        // [REFRESH BUTTON HANDLER] 사진이 없을 때 대기 화면 버튼
        if (photoFiles.size() == 0) {
            if (tx >= 240 && tx <= 310 && ty >= 80 && ty <= 130) {
                Serial.println("Refresh button pressed!");
                tft.fillRoundRect(100, 185, 120, 35, 6, TFT_BLUE);
                tft.setTextColor(TFT_WHITE);
                tft.drawString("REFRESH", 160, 202, 2);
                delay(300);
                
                loadPhotoList();
                isShowingPhoto = false;
                return;
            }
        }
        
        // [DEL BUTTON HANDLER] DEL 버튼이 표시된 상태일 때만 처리
        if (isDelVisible) {
            if (tx >= 220 && tx <= 320 && ty >= 150 && ty <= 240) {
                Serial.println("Trash icon pressed!");
                isDelVisible = false;
                
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_RED);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("Deleting Photo...", 160, 120, 4);
                delay(500);
                
                deleteCurrentPhoto();
                return;
            } else if (millis() - touchStartTime > 200) {
                // DEL 버튼 바깥 화면을 누르면 DEL 버튼 숨김
                isDelVisible = false;
                if (photoFiles.size() > 0) {
                    showPhoto(photoFiles[currentPhotoIndex]);
                }
                return;
            }
        }
    } else {
        if (isTouching) {
            isTouching = false;
            lastTapReleaseTime = millis();
        }
    }
}

void deleteCurrentPhoto() {
    if (currentPhotoIndex < 0 || currentPhotoIndex >= (int)photoFiles.size()) {
        return;
    }
    
    String filepath = photoFiles[currentPhotoIndex];
    Serial.printf("Touch Delete Action: removing %s\n", filepath.c_str());
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    
    if (SD.exists(filepath)) {
        bool success = SD.remove(filepath);
        if (success) {
            tft.setTextColor(TFT_GREEN);
            tft.drawString("Photo Deleted!", 160, 120, 4);
        } else {
            tft.setTextColor(TFT_RED);
            tft.drawString("Delete Failed!", 160, 100, 4);
            tft.drawString("Card might be Read-Only", 160, 140, 2);
        }
    } else {
        tft.setTextColor(TFT_RED);
        tft.drawString("File Not Found!", 160, 120, 4);
    }
    delay(1500);
    
    loadPhotoList();
    isShowingPhoto = false; 
    lastPhotoSwitchTime = millis() - slideshowInterval; 
}