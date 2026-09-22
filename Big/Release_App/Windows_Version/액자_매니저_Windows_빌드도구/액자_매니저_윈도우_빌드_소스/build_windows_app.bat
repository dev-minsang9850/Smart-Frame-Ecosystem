@echo off
echo ========================================================
echo Windows App Build Tool
echo ========================================================
echo.
echo 1. Installing required libraries...
python -m pip install --upgrade pip
python -m pip install pyqt5 pyserial pillow pyinstaller

echo.
echo 2. Building the standalone .exe app...
python -m PyInstaller --windowed --onefile --noconfirm --name "SmartFrameManager" main.py

echo.
echo ========================================================
echo Build complete! Check the 'dist' folder for SmartFrameManager.exe
echo ========================================================
pause
