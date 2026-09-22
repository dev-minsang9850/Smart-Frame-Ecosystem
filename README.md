# 📸 PixShare & Smart Frame 통합 프로젝트

본 문서는 로컬 사진 공유 웹앱(PixShare)과 스마트 액자 제어 데스크톱 프로그램(Smart Frame Manager)을 융합한 엔드투엔드(End-to-End) 사진 공유/재생 플랫폼의 전체 개발 스토리를 담은 문서입니다.

---

## 1. 프로젝트 기본 정보
- **프로젝트명**: PixShare & Smart Frame Manager
- **작업 디렉토리**: `/Users/paulee/2026_Projects/new_projects/Finish/arduino_esp32_frame`
- **목적**: 스마트폰의 사진을 로컬 웹(PixShare)을 통해 PC로 간편하게 모으고, 이를 다시 아두이노 기반 전자 액자에 최적화하여 띄워주는(Smart Frame Manager) 완벽한 하드웨어-소프트웨어 연동 생태계 구축.
- **기술 스택**:
  - **웹 프론트/백엔드 (PixShare)**: Node.js (Express, multer), Vanilla JS, HTML5, CSS3
  - **PC 매니저 앱 (Smart Frame)**: Python 3, PyQt5, PyInstaller, Pillow
  - **하드웨어 펌웨어 (액자)**: Arduino C++, ESP32 (TFT_eSPI, TJpg_Decoder)

---

## 2. 전체 개발 스토리 (기획 ~ 완성)

### [Phase 0] 프로젝트 시작과 초기 기획 (Planning)
- **요구사항 발생**: 기존 작업 디렉토리에는 전자액자 실습용 가이드 문서만 존재했으나, "휴대폰 사진을 선 연결 없이 PC로 쉽게 옮기고, 이를 곧바로 액자 펌웨어에 맞게 변환해주는 올인원 시스템"의 필요성이 대두됨.
- **기획 문서 작성**:
  - 모바일에서 QR 코드를 찍고 접속해 사진을 올릴 수 있는 **로컬 네트워크 기반 Express 서버 (PixShare)**를 기획하고 다크 테마/글래스모피즘 UI를 적용.
  - 수집된 사진을 320x240 해상도로 자동 리사이징하고 아두이노용 순정 JPG로 변환하는 **PC 데스크톱 매니저** 앱 구조 설계.

### [Phase 1] 뼈대 구축 및 개발 (Execution)
- **웹 앱 (PixShare)**: `express`, `multer` 기반으로 로컬 IPv4를 감지해 업로드 갤러리 API와 뷰를 구현 (`server.js`, `public/` 등).
- **매니저 앱 (Smart Frame)**: PyQt5를 이용해 '연결 방식(USB/SD)'을 고르는 UI를 만들고, 내부적으로 사진을 RGB JPEG로 변환해 하드웨어 기기로 쏘아주는 Python 로직 구축 (`PC_Manager_App/main.py`).

### [Phase 2] 트러블슈팅 및 버그 픽스 (Troubleshooting)
순조로워 보이던 개발 과정에서 웹, 앱, 하드웨어 전반에 걸쳐 5가지 치명적인 이슈를 겪고 극복했습니다.

1. **[이슈 1] Express 5.x 와일드카드 라우팅 에러 (서버 크래시)**
   - *상황*: SPA 대응용 `app.get('*')`가 내부 `path-to-regexp` 모듈과 충돌하여 서버가 즉시 뻗어버림.
   - *해결*: 해당 라우팅 로직을 `app.get('/')`로 안전하게 수정하여 정상 구동시킴.
2. **[이슈 2] 드롭존 클릭 무한 루프 (Event Bubbling 차단)**
   - *상황*: 사진 업로드 영역을 클릭해도 이벤트가 자식 `<input type="file">`과 중첩되어 무한 버블링 발생, 브라우저가 클릭 차단.
   - *해결*: `<input>` 태그를 밖으로 빼고, 명시적인 '사진 파일 선택하기' 버튼을 도입해 이벤트 리스너 재연결.
3. **[이슈 3] 프론트엔드 버튼 먹통 (스크립트 태그 누락)**
   - *상황*: 가장 어이없는 실수로 HTML 문서 하단에 `<script src="app.js"></script>`가 누락되어 있었음.
   - *해결*: 스크립트 태그를 삽입해 클라이언트 로직 정상 복구.
4. **[이슈 4] 액자 사진 누움 및 포맷 에러 (EXIF 메타데이터 찌꺼기)**
   - *상황*: 아이폰으로 찍은 세로 사진이 액자에서는 목이 꺾인 채 가로로 나오거나 HEIC 포맷을 아두이노가 거부함.
   - *해결*: 매니저 앱에서 EXIF 꼬리표의 회전 정보를 읽어 이미지를 강제로 세운 뒤, EXIF 메타데이터를 삭제하고 순정 RGB JPEG 포맷으로 변환해 액자로 넘기도록 Python 로직 추가.
5. **[이슈 5] Mac OS 다크모드 및 PyQt5 렌더링 충돌**
   - *상황*: Mac 환경에서 매니저 앱 구동 시 버튼이 새카맣게 타버리고 텍스트 뒤에 흉측한 흰색 네모 박스 발생 (CSS 상속 버그).
   - *해결*: `QApplication.setStyle('Fusion')` 옵션을 강제 적용하여 OS 상관없이 동일한 테마를 렌더링하고, 레이아웃을 래핑(Wrapping)하여 완벽히 교정.

### [Phase 3] 검증 및 문서화 완료 (Verification)
- 모바일을 통한 PixShare 파일 업로드 정상 동작 확인 완료.
- 통합 매니저 앱의 UI 최적화(직관적 번호 가이드 부여 및 글꼴 확대) 후 Windows(`.exe`)와 Mac(`.app`) 크로스 플랫폼 단일 파일 패키징 완료 (`Release_App` 내 보관).

---

## 3. GitHub 업로드 안내
현재 이 프로젝트는 기획부터 디버깅까지 완벽한 스토리를 담아 `.gitignore` 셋팅이 완료되었습니다. `Release_App`, `PC_Manager_App`, `arduino_frame_firmware_serial` 등 모든 필수 코드가 리포지토리에 반영됩니다.
