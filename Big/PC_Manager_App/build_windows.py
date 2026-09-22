import os
import subprocess
import sys

def run_cmd(cmd):
    print(f"Running: {cmd}")
    subprocess.check_call(cmd, shell=True)

if __name__ == "__main__":
    print("="*50)
    print("스마트 액자 매니저 - Windows 앱 빌드 도구")
    print("="*50)
    try:
        print("\n[1/2] 필요한 라이브러리 설치 중...")
        run_cmd(f"{sys.executable} -m pip install --upgrade pip")
        run_cmd(f"{sys.executable} -m pip install pyqt5 pyserial pillow pyinstaller")
        
        print("\n[2/2] 앱 빌드 중... (잠시만 기다려주세요)")
        run_cmd(f"{sys.executable} -m PyInstaller --windowed --onefile --noconfirm --name \"액자 매니저 앱\" main.py")
        
        print("\n" + "="*50)
        print("🎉 빌드가 완료되었습니다!")
        print("'dist' 폴더 안에 생성된 '액자 매니저 앱.exe'를 확인하세요.")
        print("="*50)
    except Exception as e:
        print(f"\n[오류 발생] {e}")
    
    input("\n창을 닫으려면 엔터키를 누르세요...")
