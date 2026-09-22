import sys
import os
import platform
import subprocess
import time
import io
import shutil
import serial
import serial.tools.list_ports
from PyQt5.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                             QLabel, QPushButton, QStackedWidget, QMessageBox, QProgressBar, QFileDialog, QComboBox, QRadioButton, QButtonGroup, QFrame)
from PyQt5.QtCore import Qt, QThread, pyqtSignal
from PyQt5.QtGui import QFont
from PIL import Image, ImageOps

SYSTEM = platform.system()

def get_app_data_dir():
    home = os.path.expanduser("~")
    if SYSTEM == "Windows":
        path = os.path.join(os.getenv("APPDATA", home), "SmartFrameUnifiedManager")
    elif SYSTEM == "Darwin":
        path = os.path.join(home, "Library", "Application Support", "SmartFrameUnifiedManager")
    else:
        path = os.path.join(home, ".config", "SmartFrameUnifiedManager")
    if not os.path.exists(path):
        os.makedirs(path)
    return path

# --- USB Serial Threads ---
class SerialMonitorThread(QThread):
    connected_signal = pyqtSignal(str)
    
    def __init__(self):
        super().__init__()
        self.running = True
        self.port_name = None
        
    def run(self):
        while self.running:
            ports = serial.tools.list_ports.comports()
            found_port = None
            for p in ports:
                desc = p.description.lower()
                hwid = p.hwid.lower()
                # Check for standard ESP32/CH340/CP210x/CDC matches
                if "ch340" in desc or "cp210" in desc or "uart" in desc or "usb" in desc or "serial" in desc or "ch340" in hwid or "cp210" in hwid or "vid:pid" in hwid:
                    found_port = p.device
                    break
            
            if found_port != self.port_name:
                self.port_name = found_port
                if self.port_name:
                    self.connected_signal.emit(self.port_name)
                else:
                    self.connected_signal.emit("")
            time.sleep(1)
            
    def stop(self):
        self.running = False

class SyncWorkerThread(QThread):
    progress_signal = pyqtSignal(int)
    log_signal = pyqtSignal(str)
    finished_signal = pyqtSignal(bool, str)
    
    def __init__(self, port_name, filepaths, interval_ms):
        super().__init__()
        self.port = port_name
        self.filepaths = filepaths
        self.interval_ms = interval_ms
        self.upload_complete = False
        
    def run(self):
        try:
            self.ser = serial.Serial(self.port, 115200, timeout=6)
            time.sleep(2)
            
            if self.filepaths is None:
                self.auto_sync()
            else:
                self.upload_new()
                
            if self.upload_complete:
                self.log_signal.emit("설정 적용 중...")
                config_data = f"{self.interval_ms}\n".encode('utf-8')
                self._upload_bytes("config.txt", config_data)
                self.finished_signal.emit(True, "작업 및 설정이 완벽하게 적용되었습니다! 액자를 확인하세요.")
            
        except Exception as e:
            self.finished_signal.emit(False, str(e))
        finally:
            if hasattr(self, 'ser') and self.ser.is_open:
                self.ser.close()

    def _upload_bytes(self, filename, data):
        self.ser.write(f"UPLOAD:{filename}:{len(data)}\n".encode())
        time.sleep(0.5)
        
        chunk_size = 512
        for i in range(0, len(data), chunk_size):
            chunk = data[i:i+chunk_size]
            self.ser.write(chunk)
            time.sleep(0.01)
            
        start_wait = time.time()
        while time.time() - start_wait < 6.0:
            if self.ser.in_waiting > 0:
                resp = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if "ACK:SUCCESS" in resp:
                    return True
        raise Exception("액자 응답 시간 초과!")

    def auto_sync(self):
        self.log_signal.emit("액자 내부 파일 스캔 중...")
        self.ser.write(b"LIST\n")
        
        files_to_process = []
        start_t = time.time()
        while time.time() - start_t < 3.0:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line == "END_LIST": break
                if line.startswith("FILE:"):
                    parts = line.split(":")
                    if len(parts) == 3:
                        name, size = parts[1], int(parts[2])
                        if size > 250000:
                            files_to_process.append(name)
        
        if not files_to_process:
            self.upload_complete = True
            return

        backup_dir = os.path.join(os.path.expanduser("~"), "Desktop", "액자_원본_백업")
        if not os.path.exists(backup_dir): os.makedirs(backup_dir)

        for idx, filename in enumerate(files_to_process):
            self.log_signal.emit(f"({idx+1}/{len(files_to_process)}) {filename} 백업 중...")
            self.ser.write(f"DOWNLOAD:{filename}\n".encode())
            
            resp = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if not resp.startswith("SIZE:"): continue
            
            filesize = int(resp.split(":")[1])
            self.ser.write(b"ACK\n")
            
            received = b""
            while len(received) < filesize:
                chunk = self.ser.read(min(1024, filesize - len(received)))
                if not chunk: break
                received += chunk
                
            backup_path = os.path.join(backup_dir, filename)
            with open(backup_path, "wb") as f:
                f.write(received)
                
            self.log_signal.emit(f"({idx+1}/{len(files_to_process)}) 최적화 및 재전송 중...")
            img = Image.open(io.BytesIO(received))
            img = ImageOps.exif_transpose(img)
            img.thumbnail((320, 240), Image.Resampling.LANCZOS)
            img = img.convert('RGB')
            
            buf = io.BytesIO()
            img.save(buf, format="JPEG", quality=85)
            opt_data = buf.getvalue()
            
            self._upload_bytes(filename, opt_data)
            self.progress_signal.emit(int(((idx+1) / len(files_to_process)) * 100))
            
        self.upload_complete = True
        self.progress_signal.emit(100)

    def upload_new(self):
        for idx, filepath in enumerate(self.filepaths):
            original_name = os.path.basename(filepath)
            self.log_signal.emit(f"({idx+1}/{len(self.filepaths)}) {original_name} 최적화 및 전송 중...")
            
            clean_name = f"photo_new_{int(time.time())}_{idx}.jpg"
            
            img = Image.open(filepath)
            img = ImageOps.exif_transpose(img)
            img.thumbnail((320, 240), Image.Resampling.LANCZOS)
            img = img.convert('RGB')
            
            buf = io.BytesIO()
            img.save(buf, format="JPEG", quality=85)
            opt_data = buf.getvalue()
            
            self._upload_bytes(clean_name, opt_data)
            self.progress_signal.emit(int(((idx+1) / len(self.filepaths)) * 100))
            
        self.upload_complete = True
        self.progress_signal.emit(100)


# --- SD Card Worker Thread ---
class SDWorkerThread(QThread):
    progress_signal = pyqtSignal(int)
    log_signal = pyqtSignal(str)
    finished_signal = pyqtSignal(bool, str)
    
    def __init__(self, filepaths, interval_ms, sd_root):
        super().__init__()
        self.filepaths = filepaths
        self.interval_ms = interval_ms
        self.sd_root = sd_root
        
    def run(self):
        try:
            if self.filepaths is None:
                self.auto_sync()
            else:
                self.upload_new()
                
            self.log_signal.emit("설정 적용 중...")
            config_path = os.path.join(self.sd_root, "config.txt")
            with open(config_path, "w", encoding='utf-8') as f:
                f.write(f"{self.interval_ms}\n")
            time.sleep(0.5)
            
            self.finished_signal.emit(True, "작업 및 설정이 완벽하게 적용되었습니다!\n이제 SD카드를 뽑아서 액자에 꽂아주세요.")
        except Exception as e:
            self.finished_signal.emit(False, str(e))

    def auto_sync(self):
        self.log_signal.emit("SD카드 사진 목록 확인 중...")
        time.sleep(0.5)
        
        files_to_process = []
        for f in os.listdir(self.sd_root):
            if f.lower().endswith(('.jpg', '.jpeg', '.png')):
                full_path = os.path.join(self.sd_root, f)
                size = os.path.getsize(full_path)
                if size > 250000: # 250KB 이상
                    files_to_process.append(f)

        if not files_to_process:
            pass

        backup_dir = os.path.join(os.path.expanduser("~"), "Desktop", "액자_원본_백업")
        if not os.path.exists(backup_dir): os.makedirs(backup_dir)

        for idx, filename in enumerate(files_to_process):
            self.log_signal.emit(f"({idx+1}/{len(files_to_process)}) {filename} 백업 및 최적화 중...")
            self.progress_signal.emit(0)
            
            old_path = os.path.join(self.sd_root, filename)
            backup_path = os.path.join(backup_dir, filename)
            
            shutil.copy2(old_path, backup_path)
            
            try:
                img = Image.open(old_path)
                img = ImageOps.exif_transpose(img)
                img.thumbnail((320, 240), Image.Resampling.LANCZOS)
                img = img.convert('RGB')
                
                os.remove(old_path)
                
                clean_name = f"photo_opt_{int(time.time())}_{idx}.jpg"
                new_path = os.path.join(self.sd_root, clean_name)
                
                img.save(new_path, format="JPEG", quality=85)
            except Exception as e:
                self.log_signal.emit(f"최적화 실패: {filename}")
            
            self.progress_signal.emit(int(((idx+1) / len(files_to_process)) * 100))
            
        self.progress_signal.emit(100)

    def upload_new(self):
        for idx, filepath in enumerate(self.filepaths):
            original_name = os.path.basename(filepath)
            self.log_signal.emit(f"({idx+1}/{len(self.filepaths)}) {original_name} 저장 중...")
            
            clean_name = f"photo_new_{int(time.time())}_{idx}.jpg"
            new_path = os.path.join(self.sd_root, clean_name)
            
            try:
                img = Image.open(filepath)
                img = ImageOps.exif_transpose(img)
                img.thumbnail((320, 240), Image.Resampling.LANCZOS)
                img = img.convert('RGB')
                
                img.save(new_path, format="JPEG", quality=85)
            except Exception as e:
                self.log_signal.emit(f"오류: {e}")
                continue
                
            self.progress_signal.emit(int(((idx+1) / len(self.filepaths)) * 100))
            
        self.progress_signal.emit(100)

# --- App UI ---
class ModernApp(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("스마트 액자 통합 매니저")
        self.resize(550, 450)
        self.setStyleSheet("""
            QWidget {
                background-color: #f5f6f8;
                font-family: Arial;
                color: #333333;
                font-size: 16px;
            }
            QLabel {
                background-color: transparent;
                color: #333333;
                font-size: 16px;
            }
            QPushButton {
                background-color: #2D8C57;
                color: white;
                border: none;
                padding: 16px;
                border-radius: 8px;
                font-weight: bold;
                font-size: 18px;
            }
            QPushButton:hover {
                background-color: #35A365;
            }
            QPushButton:pressed {
                background-color: #246B43;
            }
            QPushButton:disabled {
                background-color: #d1d5db; 
                color: #9ca3af;
            }
            QRadioButton {
                background-color: transparent;
                font-size: 18px;
                font-weight: bold;
                color: #333333;
                spacing: 10px;
            }
            QRadioButton::indicator {
                width: 22px;
                height: 22px;
            }
            QComboBox {
                background-color: white;
                color: #333333;
                border: 1px solid #d1d5db;
                border-radius: 4px;
                padding: 6px;
                font-size: 15px;
            }
            QComboBox QAbstractItemView {
                background-color: white;
                color: #333333;
                selection-background-color: #2D8C57;
                selection-color: white;
            }
        """)
        
        self.port_name = None
        self.sd_root = None
        self.current_mode = "USB"
        
        self.layout = QVBoxLayout()
        self.setLayout(self.layout)
        
        self.stack = QStackedWidget()
        self.layout.addWidget(self.stack)
        
        self.setup_wizard_step1()
        self.setup_wizard_step2()
        self.setup_wizard_step3()
        self.setup_sync_app()
        
        marker_path = os.path.join(get_app_data_dir(), "installed.dat")
        if os.path.exists(marker_path):
            self.stack.setCurrentIndex(3)
            self.on_mode_changed()
        else:
            self.stack.setCurrentIndex(0)

    def create_title(self, text):
        lbl = QLabel(text)
        lbl.setFont(QFont("Arial", 20, QFont.Bold))
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setStyleSheet("color: #2D8C57; margin-bottom: 10px;")
        return lbl

    def setup_wizard_step1(self):
        page = QWidget()
        l = QVBoxLayout(page)
        l.addWidget(self.create_title("환영합니다!"))
        
        msg = QLabel("스마트 액자 통합 매니저입니다.\n\n이 프로그램은 사진을 액자에 알맞게 최적화하여 전송합니다.")
        msg.setAlignment(Qt.AlignCenter)
        msg.setStyleSheet("font-size: 14px; line-height: 1.5;")
        l.addWidget(msg)
        
        btn = QPushButton("다음")
        btn.clicked.connect(lambda: self.stack.setCurrentIndex(1))
        
        h = QHBoxLayout()
        h.addStretch()
        h.addWidget(btn)
        l.addLayout(h)
        self.stack.addWidget(page)

    def setup_wizard_step2(self):
        page = QWidget()
        l = QVBoxLayout(page)
        l.addWidget(self.create_title("자동 백업 안내"))
        
        msg = QLabel("액자 내부의 원본 사진을 변환할 때,\n원본 사진은 바탕화면의 [액자_원본_백업] 폴더에\n자동으로 안전하게 보관됩니다.")
        msg.setAlignment(Qt.AlignCenter)
        msg.setStyleSheet("font-size: 14px; line-height: 1.5;")
        l.addWidget(msg)
        
        btn_prev = QPushButton("이전")
        btn_prev.setStyleSheet("background-color: #888888;")
        btn_prev.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        
        btn_next = QPushButton("다음")
        btn_next.clicked.connect(lambda: self.stack.setCurrentIndex(2))
        
        h = QHBoxLayout()
        h.addStretch()
        h.addWidget(btn_prev)
        h.addWidget(btn_next)
        l.addLayout(h)
        self.stack.addWidget(page)

    def setup_wizard_step3(self):
        page = QWidget()
        l = QVBoxLayout(page)
        l.addWidget(self.create_title("준비 완료!"))
        
        msg = QLabel("이제 사진을 추가하거나 최적화할 준비가 되었습니다.\n\n프로그램을 시작하시겠습니까?")
        msg.setAlignment(Qt.AlignCenter)
        msg.setStyleSheet("font-size: 14px; line-height: 1.5;")
        l.addWidget(msg)
        
        btn = QPushButton("프로그램 켜기")
        btn.clicked.connect(self.finish_installation)
        
        h = QHBoxLayout()
        h.addStretch()
        h.addWidget(btn)
        l.addLayout(h)
        self.stack.addWidget(page)

    def finish_installation(self):
        marker_path = os.path.join(get_app_data_dir(), "installed.dat")
        try:
            with open(marker_path, "w") as f:
                f.write("installed")
        except: pass
        self.stack.setCurrentIndex(3)
        self.on_mode_changed()

    def setup_sync_app(self):
        page = QWidget()
        l = QVBoxLayout(page)
        l.addWidget(self.create_title("스마트 액자 매니저"))
        
                # Mode Selection
        mode_frame = QFrame()
        mode_frame.setObjectName("ModeFrame")
        mode_frame.setStyleSheet("QFrame#ModeFrame { background-color: #e0e0e0; border-radius: 8px; }")
        mode_layout = QHBoxLayout(mode_frame)
        mode_layout.setContentsMargins(20, 15, 20, 15)
        
        lbl = QLabel("1. 연결 방식:")
        lbl.setStyleSheet("font-weight: bold; font-size: 14px; color: #333333; margin-right: 15px;")
        mode_layout.addWidget(lbl)
        
        self.radio_usb = QRadioButton("① USB 케이블 모드")
        self.radio_sd = QRadioButton("② SD 카드 직접 모드")
        self.radio_usb.setChecked(True)
        
        mode_layout.addWidget(self.radio_usb)
        mode_layout.addSpacing(20)
        mode_layout.addWidget(self.radio_sd)
        
        self.mode_group = QButtonGroup()
        self.mode_group.addButton(self.radio_usb)
        self.mode_group.addButton(self.radio_sd)
        self.radio_usb.toggled.connect(self.on_mode_changed)
        
        mode_wrapper = QHBoxLayout()
        mode_wrapper.addStretch()
        mode_wrapper.addWidget(mode_frame)
        mode_wrapper.addStretch()
        l.addLayout(mode_wrapper)
        
        # Dynamic Status Area
        self.status_lbl = QLabel("")
        self.status_lbl.setFont(QFont("Arial", 12))
        self.status_lbl.setAlignment(Qt.AlignCenter)
        l.addWidget(self.status_lbl)
        
        self.select_sd_btn = QPushButton("📁 SD 카드 폴더(드라이브) 선택하기")
        self.select_sd_btn.clicked.connect(self.select_sd_card_folder)
        self.select_sd_btn.setStyleSheet("background-color: #0078D7; color: white; padding: 10px;")
        self.select_sd_btn.hide()
        l.addWidget(self.select_sd_btn)
        
        h_interval = QHBoxLayout()
        h_interval.addWidget(QLabel("2. 사진 슬라이드쇼 간격:"))
        self.interval_combo = QComboBox()
        self.interval_combo.addItems(["3초", "5초", "10초", "30초", "60초"])
        self.interval_combo.setCurrentText("10초")
        h_interval.addWidget(self.interval_combo)
        l.addLayout(h_interval)
        
        self.sync_btn = QPushButton("3. 기존 사진 자동 최적화")
        self.sync_btn.setEnabled(False)
        self.sync_btn.clicked.connect(self.start_auto_sync)
        
        self.upload_btn = QPushButton("4. 새 사진 추가하기")
        self.upload_btn.setEnabled(False)
        self.upload_btn.clicked.connect(self.start_upload_new)
        
        l.addWidget(self.sync_btn)
        l.addWidget(self.upload_btn)
        
        self.prog = QProgressBar()
        self.prog.setValue(0)
        l.addWidget(self.prog)
        
        self.log_lbl = QLabel("대기 중...")
        self.log_lbl.setAlignment(Qt.AlignCenter)
        l.addWidget(self.log_lbl)
        
        self.stack.addWidget(page)
        
        # Start the USB monitor but keep it paused logically if mode is SD
        self.monitor = SerialMonitorThread()
        self.monitor.connected_signal.connect(self.on_usb_connected)
        self.monitor.start()

    def on_mode_changed(self):
        if self.radio_usb.isChecked():
            self.current_mode = "USB"
            self.select_sd_btn.hide()
            if self.port_name:
                self.status_lbl.setText("✅ 액자가 USB로 연결되었습니다.")
                self.status_lbl.setStyleSheet("color: #2D8C57; font-weight: bold;")
                self.set_ui_enabled(True)
            else:
                self.status_lbl.setText("액자를 USB 케이블로 컴퓨터에 연결해 주세요.")
                self.status_lbl.setStyleSheet("color: #333333; font-weight: normal;")
                self.set_ui_enabled(False)
        else:
            self.current_mode = "SD"
            self.select_sd_btn.show()
            if self.sd_root:
                self.status_lbl.setText(f"✅ SD 경로: {self.sd_root}")
                self.status_lbl.setStyleSheet("color: #2D8C57; font-weight: bold;")
                self.set_ui_enabled(True)
            else:
                self.status_lbl.setText("컴퓨터에 연결된 SD 카드를 선택해 주세요.")
                self.status_lbl.setStyleSheet("color: #333333; font-weight: normal;")
                self.set_ui_enabled(False)

    def select_sd_card_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "SD 카드 드라이브/폴더를 선택하세요")
        if folder:
            self.sd_root = folder
            self.on_mode_changed()

    def on_usb_connected(self, port):
        self.port_name = port
        if self.current_mode == "USB":
            self.on_mode_changed()

    def set_ui_enabled(self, enabled):
        self.sync_btn.setEnabled(enabled)
        self.upload_btn.setEnabled(enabled)
        self.interval_combo.setEnabled(enabled)
        self.radio_usb.setEnabled(enabled)
        self.radio_sd.setEnabled(enabled)

    def _get_interval_ms(self):
        txt = self.interval_combo.currentText()
        interval_map = {"3초": 3000, "5초": 5000, "10초": 10000, "30초": 30000, "60초": 60000}
        return interval_map.get(txt, 10000)

    def start_auto_sync(self):
        self.set_ui_enabled(False)
        self.prog.setValue(0)
        
        if self.current_mode == "USB":
            self.worker = SyncWorkerThread(self.port_name, None, self._get_interval_ms())
        else:
            self.worker = SDWorkerThread(None, self._get_interval_ms(), self.sd_root)
            
        self.worker.progress_signal.connect(self.prog.setValue)
        self.worker.log_signal.connect(self.log_lbl.setText)
        self.worker.finished_signal.connect(self.on_worker_finished)
        self.worker.start()

    def start_upload_new(self):
        files, _ = QFileDialog.getOpenFileNames(self, "사진 선택", "", "Images (*.png *.jpg *.jpeg)")
        if not files: return
            
        self.set_ui_enabled(False)
        self.prog.setValue(0)
        
        if self.current_mode == "USB":
            self.worker = SyncWorkerThread(self.port_name, files, self._get_interval_ms())
        else:
            self.worker = SDWorkerThread(files, self._get_interval_ms(), self.sd_root)
            
        self.worker.progress_signal.connect(self.prog.setValue)
        self.worker.log_signal.connect(self.log_lbl.setText)
        self.worker.finished_signal.connect(self.on_worker_finished)
        self.worker.start()

    def on_worker_finished(self, success, msg):
        self.set_ui_enabled(True)
        if success:
            QMessageBox.information(self, "성공", msg)
            self.log_lbl.setText("작업이 완료되었습니다.")
        else:
            QMessageBox.critical(self, "오류", msg)
            self.log_lbl.setText("오류가 발생했습니다.")
            
    def closeEvent(self, event):
        self.monitor.stop()
        self.monitor.wait()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle('Fusion')
    window = ModernApp()
    window.show()
    sys.exit(app.exec_())
