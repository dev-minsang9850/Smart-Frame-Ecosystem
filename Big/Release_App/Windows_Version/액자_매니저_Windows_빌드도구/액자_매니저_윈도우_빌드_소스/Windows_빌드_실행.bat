@echo off
echo ========================================================
echo       스마트 액자 매니저 - Windows 앱(.exe) 빌드 도구
echo ========================================================
echo.
echo Python 및 pip 확인 중...
python --version >nul 2>&1
if errorlevel 1 (
    echo [오류] Python이 설치되어 있지 않거나 환경 변수(PATH)에 등록되지 않았습니다!
    echo python.org 에서 Python을 설치하실 때 반드시 "Add Python to PATH"에 체크해주세요.
    pause
    exit /b
)

echo 필요한 라이브러리를 설치합니다...
python -m pip install --upgrade pip
python -m pip install pyqt5 pyserial pillow pyinstaller

echo.
echo 앱 빌드를 시작합니다...
python -m PyInstaller --windowed --noconfirm --name "SmartFrameManager" main.py

echo.
echo ========================================================
echo 빌드가 완료되었습니다! 'dist' 폴더 안의 'SmartFrameManager.exe'를 확인하세요.
echo ========================================================
pause
