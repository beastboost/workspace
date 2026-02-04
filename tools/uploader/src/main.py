#!/usr/bin/env python3
"""
GBA Emulator Uploader for ESP32-P4
Desktop application for compiling and flashing firmware
"""

import sys
import os
import subprocess
import threading
import json
import glob
import serial
import serial.tools.list_ports
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QComboBox, QTextEdit, QProgressBar,
    QFileDialog, QTabWidget, QGroupBox, QLineEdit, QCheckBox,
    QMessageBox, QListWidget, QListWidgetItem, QSplitter, QFrame
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt6.QtGui import QFont, QIcon, QColor, QPalette


class CommandRunner(QThread):
    """Thread for running shell commands"""
    output_signal = pyqtSignal(str)
    finished_signal = pyqtSignal(bool, str)
    progress_signal = pyqtSignal(int)

    def __init__(self, command, cwd=None):
        super().__init__()
        self.command = command
        self.cwd = cwd
        self.process = None

    def run(self):
        try:
            self.process = subprocess.Popen(
                self.command,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=self.cwd,
                text=True,
                bufsize=1
            )

            for line in iter(self.process.stdout.readline, ''):
                self.output_signal.emit(line.rstrip())
                # Parse progress from ESP-IDF output
                if '[' in line and '%]' in line:
                    try:
                        pct = int(line.split('[')[1].split('%')[0].strip())
                        self.progress_signal.emit(pct)
                    except:
                        pass

            self.process.wait()
            success = self.process.returncode == 0
            self.finished_signal.emit(success, "Command completed" if success else "Command failed")

        except Exception as e:
            self.finished_signal.emit(False, str(e))

    def stop(self):
        if self.process:
            self.process.terminate()


class SerialMonitor(QThread):
    """Thread for serial port monitoring"""
    data_signal = pyqtSignal(str)

    def __init__(self, port, baudrate=115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.running = False
        self.serial = None

    def run(self):
        self.running = True
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            while self.running:
                if self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting).decode('utf-8', errors='replace')
                    self.data_signal.emit(data)
        except Exception as e:
            self.data_signal.emit(f"\n[Error: {e}]\n")
        finally:
            if self.serial:
                self.serial.close()

    def stop(self):
        self.running = False


class GBAUploaderApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("GBA Emulator Uploader - ESP32-P4")
        self.setMinimumSize(900, 700)

        self.project_path = None
        self.command_runner = None
        self.serial_monitor = None

        self.setup_ui()
        self.load_settings()
        self.refresh_ports()

        # Auto-refresh ports every 2 seconds
        self.port_timer = QTimer()
        self.port_timer.timeout.connect(self.refresh_ports)
        self.port_timer.start(2000)

    def setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # Create tabs
        tabs = QTabWidget()
        layout.addWidget(tabs)

        # Build & Flash tab
        build_tab = self.create_build_tab()
        tabs.addTab(build_tab, "Build & Flash")

        # ROM Manager tab
        rom_tab = self.create_rom_tab()
        tabs.addTab(rom_tab, "ROM Manager")

        # Serial Monitor tab
        serial_tab = self.create_serial_tab()
        tabs.addTab(serial_tab, "Serial Monitor")

        # Settings tab
        settings_tab = self.create_settings_tab()
        tabs.addTab(settings_tab, "Settings")

    def create_build_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)

        # Project selection
        project_group = QGroupBox("Project")
        project_layout = QHBoxLayout(project_group)

        self.project_label = QLabel("No project selected")
        self.project_label.setStyleSheet("color: gray;")
        project_layout.addWidget(self.project_label, 1)

        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self.browse_project)
        project_layout.addWidget(browse_btn)

        layout.addWidget(project_group)

        # Port selection
        port_group = QGroupBox("Device")
        port_layout = QHBoxLayout(port_group)

        port_layout.addWidget(QLabel("Port:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(200)
        port_layout.addWidget(self.port_combo)

        refresh_btn = QPushButton("↻")
        refresh_btn.setMaximumWidth(30)
        refresh_btn.clicked.connect(self.refresh_ports)
        port_layout.addWidget(refresh_btn)

        port_layout.addStretch()

        layout.addWidget(port_group)

        # Build options
        options_group = QGroupBox("Options")
        options_layout = QHBoxLayout(options_group)

        self.clean_check = QCheckBox("Clean build")
        options_layout.addWidget(self.clean_check)

        self.monitor_check = QCheckBox("Open monitor after flash")
        self.monitor_check.setChecked(True)
        options_layout.addWidget(self.monitor_check)

        options_layout.addStretch()

        layout.addWidget(options_group)

        # Action buttons
        btn_layout = QHBoxLayout()

        self.build_btn = QPushButton("Build")
        self.build_btn.setMinimumHeight(40)
        self.build_btn.clicked.connect(self.build_project)
        btn_layout.addWidget(self.build_btn)

        self.flash_btn = QPushButton("Flash")
        self.flash_btn.setMinimumHeight(40)
        self.flash_btn.clicked.connect(self.flash_project)
        btn_layout.addWidget(self.flash_btn)

        self.build_flash_btn = QPushButton("Build && Flash")
        self.build_flash_btn.setMinimumHeight(40)
        self.build_flash_btn.setStyleSheet("background-color: #4CAF50; color: white;")
        self.build_flash_btn.clicked.connect(self.build_and_flash)
        btn_layout.addWidget(self.build_flash_btn)

        self.stop_btn = QPushButton("Stop")
        self.stop_btn.setMinimumHeight(40)
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(self.stop_command)
        btn_layout.addWidget(self.stop_btn)

        layout.addLayout(btn_layout)

        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        layout.addWidget(self.progress_bar)

        # Output console
        console_group = QGroupBox("Output")
        console_layout = QVBoxLayout(console_group)

        self.console = QTextEdit()
        self.console.setReadOnly(True)
        self.console.setFont(QFont("Consolas", 10))
        self.console.setStyleSheet("background-color: #1e1e1e; color: #d4d4d4;")
        console_layout.addWidget(self.console)

        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self.console.clear)
        console_layout.addWidget(clear_btn)

        layout.addWidget(console_group, 1)

        return widget

    def create_rom_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)

        # Device IP for web upload
        ip_group = QGroupBox("Device Connection")
        ip_layout = QHBoxLayout(ip_group)

        ip_layout.addWidget(QLabel("Device IP:"))
        self.device_ip = QLineEdit("192.168.1.100")
        self.device_ip.setPlaceholderText("e.g., 192.168.1.100")
        ip_layout.addWidget(self.device_ip)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.connect_device)
        ip_layout.addWidget(self.connect_btn)

        layout.addWidget(ip_group)

        # ROM list
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Local ROMs
        local_group = QGroupBox("Local ROMs")
        local_layout = QVBoxLayout(local_group)

        self.local_rom_list = QListWidget()
        local_layout.addWidget(self.local_rom_list)

        local_btn_layout = QHBoxLayout()
        add_rom_btn = QPushButton("Add ROM...")
        add_rom_btn.clicked.connect(self.add_local_rom)
        local_btn_layout.addWidget(add_rom_btn)

        self.upload_rom_btn = QPushButton("Upload →")
        self.upload_rom_btn.clicked.connect(self.upload_rom)
        local_btn_layout.addWidget(self.upload_rom_btn)

        local_layout.addLayout(local_btn_layout)
        splitter.addWidget(local_group)

        # Device ROMs
        device_group = QGroupBox("Device ROMs")
        device_layout = QVBoxLayout(device_group)

        self.device_rom_list = QListWidget()
        device_layout.addWidget(self.device_rom_list)

        device_btn_layout = QHBoxLayout()
        refresh_device_btn = QPushButton("Refresh")
        refresh_device_btn.clicked.connect(self.refresh_device_roms)
        device_btn_layout.addWidget(refresh_device_btn)

        delete_rom_btn = QPushButton("Delete")
        delete_rom_btn.clicked.connect(self.delete_device_rom)
        device_btn_layout.addWidget(delete_rom_btn)

        device_layout.addLayout(device_btn_layout)
        splitter.addWidget(device_group)

        layout.addWidget(splitter, 1)

        # Upload progress
        self.rom_progress = QProgressBar()
        self.rom_progress.setVisible(False)
        layout.addWidget(self.rom_progress)

        return widget

    def create_serial_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)

        # Serial settings
        settings_layout = QHBoxLayout()

        settings_layout.addWidget(QLabel("Port:"))
        self.serial_port_combo = QComboBox()
        self.serial_port_combo.setMinimumWidth(150)
        settings_layout.addWidget(self.serial_port_combo)

        settings_layout.addWidget(QLabel("Baud:"))
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(["115200", "921600", "2000000", "9600"])
        settings_layout.addWidget(self.baud_combo)

        self.serial_connect_btn = QPushButton("Connect")
        self.serial_connect_btn.clicked.connect(self.toggle_serial)
        settings_layout.addWidget(self.serial_connect_btn)

        settings_layout.addStretch()

        layout.addLayout(settings_layout)

        # Serial output
        self.serial_output = QTextEdit()
        self.serial_output.setReadOnly(True)
        self.serial_output.setFont(QFont("Consolas", 10))
        self.serial_output.setStyleSheet("background-color: #1e1e1e; color: #00ff00;")
        layout.addWidget(self.serial_output, 1)

        # Send command
        send_layout = QHBoxLayout()
        self.serial_input = QLineEdit()
        self.serial_input.setPlaceholderText("Enter command...")
        self.serial_input.returnPressed.connect(self.send_serial)
        send_layout.addWidget(self.serial_input)

        send_btn = QPushButton("Send")
        send_btn.clicked.connect(self.send_serial)
        send_layout.addWidget(send_btn)

        clear_serial_btn = QPushButton("Clear")
        clear_serial_btn.clicked.connect(self.serial_output.clear)
        send_layout.addWidget(clear_serial_btn)

        layout.addLayout(send_layout)

        return widget

    def create_settings_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)

        # ESP-IDF settings
        idf_group = QGroupBox("ESP-IDF Configuration")
        idf_layout = QVBoxLayout(idf_group)

        idf_path_layout = QHBoxLayout()
        idf_path_layout.addWidget(QLabel("IDF Path:"))
        self.idf_path = QLineEdit()
        self.idf_path.setPlaceholderText("e.g., ~/esp/esp-idf")
        idf_path_layout.addWidget(self.idf_path)
        idf_browse = QPushButton("Browse...")
        idf_browse.clicked.connect(self.browse_idf)
        idf_path_layout.addWidget(idf_browse)
        idf_layout.addLayout(idf_path_layout)

        layout.addWidget(idf_group)

        # Target settings
        target_group = QGroupBox("Build Target")
        target_layout = QHBoxLayout(target_group)

        target_layout.addWidget(QLabel("Target:"))
        self.target_combo = QComboBox()
        self.target_combo.addItems(["esp32p4", "esp32s3", "esp32"])
        target_layout.addWidget(self.target_combo)
        target_layout.addStretch()

        layout.addWidget(target_group)

        # Save button
        save_btn = QPushButton("Save Settings")
        save_btn.clicked.connect(self.save_settings)
        layout.addWidget(save_btn)

        layout.addStretch()

        return widget

    def refresh_ports(self):
        current_port = self.port_combo.currentText()
        current_serial = self.serial_port_combo.currentText() if hasattr(self, 'serial_port_combo') else ""

        ports = serial.tools.list_ports.comports()
        port_names = [f"{p.device} - {p.description}" for p in ports]

        self.port_combo.clear()
        self.port_combo.addItems(port_names)

        if hasattr(self, 'serial_port_combo'):
            self.serial_port_combo.clear()
            self.serial_port_combo.addItems(port_names)

        # Restore selection
        for i, name in enumerate(port_names):
            if current_port and current_port.split(' - ')[0] in name:
                self.port_combo.setCurrentIndex(i)
            if current_serial and current_serial.split(' - ')[0] in name:
                self.serial_port_combo.setCurrentIndex(i)

    def browse_project(self):
        path = QFileDialog.getExistingDirectory(self, "Select ESP-IDF Project")
        if path:
            # Check if it's a valid ESP-IDF project
            if os.path.exists(os.path.join(path, "CMakeLists.txt")):
                self.project_path = path
                self.project_label.setText(path)
                self.project_label.setStyleSheet("color: green;")
                self.log(f"Project loaded: {path}")
            else:
                QMessageBox.warning(self, "Invalid Project",
                                    "Selected directory is not a valid ESP-IDF project.\n"
                                    "CMakeLists.txt not found.")

    def browse_idf(self):
        path = QFileDialog.getExistingDirectory(self, "Select ESP-IDF Directory")
        if path:
            self.idf_path.setText(path)

    def get_port(self):
        port_text = self.port_combo.currentText()
        if port_text:
            return port_text.split(' - ')[0]
        return None

    def get_idf_command(self, cmd):
        idf_path = self.idf_path.text() or os.environ.get('IDF_PATH', '')
        if idf_path:
            export_script = os.path.join(idf_path, 'export.sh')
            return f"source {export_script} && {cmd}"
        return cmd

    def build_project(self):
        if not self.project_path:
            QMessageBox.warning(self, "No Project", "Please select a project first.")
            return

        cmd = "idf.py build"
        if self.clean_check.isChecked():
            cmd = "idf.py fullclean && " + cmd

        self.run_command(self.get_idf_command(cmd))

    def flash_project(self):
        if not self.project_path:
            QMessageBox.warning(self, "No Project", "Please select a project first.")
            return

        port = self.get_port()
        if not port:
            QMessageBox.warning(self, "No Port", "Please select a serial port.")
            return

        cmd = f"idf.py -p {port} flash"
        self.run_command(self.get_idf_command(cmd))

    def build_and_flash(self):
        if not self.project_path:
            QMessageBox.warning(self, "No Project", "Please select a project first.")
            return

        port = self.get_port()
        if not port:
            QMessageBox.warning(self, "No Port", "Please select a serial port.")
            return

        cmd = f"idf.py -p {port} flash"
        if self.clean_check.isChecked():
            cmd = "idf.py fullclean && idf.py build && " + cmd
        else:
            cmd = "idf.py build && " + cmd

        if self.monitor_check.isChecked():
            cmd += " monitor"

        self.run_command(self.get_idf_command(cmd))

    def run_command(self, command):
        self.console.clear()
        self.log(f"Running: {command}\n")

        self.set_buttons_enabled(False)
        self.progress_bar.setVisible(True)
        self.progress_bar.setValue(0)

        self.command_runner = CommandRunner(command, self.project_path)
        self.command_runner.output_signal.connect(self.log)
        self.command_runner.progress_signal.connect(self.progress_bar.setValue)
        self.command_runner.finished_signal.connect(self.command_finished)
        self.command_runner.start()

    def command_finished(self, success, message):
        self.set_buttons_enabled(True)
        self.progress_bar.setVisible(False)

        if success:
            self.log(f"\n✓ {message}")
        else:
            self.log(f"\n✗ {message}")

    def stop_command(self):
        if self.command_runner:
            self.command_runner.stop()

    def set_buttons_enabled(self, enabled):
        self.build_btn.setEnabled(enabled)
        self.flash_btn.setEnabled(enabled)
        self.build_flash_btn.setEnabled(enabled)
        self.stop_btn.setEnabled(not enabled)

    def log(self, text):
        self.console.append(text)
        self.console.verticalScrollBar().setValue(
            self.console.verticalScrollBar().maximum()
        )

    # ROM Manager functions
    def add_local_rom(self):
        files, _ = QFileDialog.getOpenFileNames(
            self, "Select ROM Files", "",
            "GBA ROMs (*.gba *.agb *.bin);;All Files (*)"
        )
        for f in files:
            item = QListWidgetItem(os.path.basename(f))
            item.setData(Qt.ItemDataRole.UserRole, f)
            self.local_rom_list.addItem(item)

    def connect_device(self):
        ip = self.device_ip.text()
        self.log(f"Connecting to {ip}...")
        self.refresh_device_roms()

    def refresh_device_roms(self):
        import urllib.request
        ip = self.device_ip.text()
        try:
            url = f"http://{ip}/api/roms"
            with urllib.request.urlopen(url, timeout=5) as response:
                data = json.loads(response.read())
                self.device_rom_list.clear()
                for rom in data.get('roms', []):
                    self.device_rom_list.addItem(rom['name'])
        except Exception as e:
            self.log(f"Failed to connect: {e}")

    def upload_rom(self):
        item = self.local_rom_list.currentItem()
        if not item:
            QMessageBox.warning(self, "No ROM", "Please select a ROM to upload.")
            return

        rom_path = item.data(Qt.ItemDataRole.UserRole)
        ip = self.device_ip.text()

        # Upload via HTTP
        self.log(f"Uploading {rom_path} to {ip}...")
        self.rom_progress.setVisible(True)

        try:
            import urllib.request
            with open(rom_path, 'rb') as f:
                data = f.read()

            req = urllib.request.Request(
                f"http://{ip}/api/upload",
                data=data,
                headers={'Content-Type': 'application/octet-stream',
                         'X-Filename': os.path.basename(rom_path)}
            )
            urllib.request.urlopen(req, timeout=60)
            self.log("Upload complete!")
            self.refresh_device_roms()
        except Exception as e:
            self.log(f"Upload failed: {e}")
        finally:
            self.rom_progress.setVisible(False)

    def delete_device_rom(self):
        item = self.device_rom_list.currentItem()
        if not item:
            return

        rom_name = item.text()
        ip = self.device_ip.text()

        try:
            import urllib.request
            req = urllib.request.Request(
                f"http://{ip}/api/delete?name={rom_name}",
                method='DELETE'
            )
            urllib.request.urlopen(req, timeout=10)
            self.refresh_device_roms()
        except Exception as e:
            self.log(f"Delete failed: {e}")

    # Serial Monitor functions
    def toggle_serial(self):
        if self.serial_monitor and self.serial_monitor.running:
            self.serial_monitor.stop()
            self.serial_connect_btn.setText("Connect")
        else:
            port = self.serial_port_combo.currentText().split(' - ')[0]
            baud = int(self.baud_combo.currentText())
            self.serial_monitor = SerialMonitor(port, baud)
            self.serial_monitor.data_signal.connect(self.serial_output.insertPlainText)
            self.serial_monitor.start()
            self.serial_connect_btn.setText("Disconnect")

    def send_serial(self):
        if self.serial_monitor and self.serial_monitor.serial:
            cmd = self.serial_input.text() + '\n'
            self.serial_monitor.serial.write(cmd.encode())
            self.serial_input.clear()

    # Settings
    def load_settings(self):
        settings_file = os.path.expanduser("~/.gba_uploader_settings.json")
        if os.path.exists(settings_file):
            try:
                with open(settings_file) as f:
                    settings = json.load(f)
                    self.idf_path.setText(settings.get('idf_path', ''))
                    self.project_path = settings.get('project_path')
                    if self.project_path:
                        self.project_label.setText(self.project_path)
                        self.project_label.setStyleSheet("color: green;")
                    self.device_ip.setText(settings.get('device_ip', '192.168.1.100'))
            except:
                pass

    def save_settings(self):
        settings_file = os.path.expanduser("~/.gba_uploader_settings.json")
        settings = {
            'idf_path': self.idf_path.text(),
            'project_path': self.project_path,
            'device_ip': self.device_ip.text()
        }
        with open(settings_file, 'w') as f:
            json.dump(settings, f, indent=2)
        QMessageBox.information(self, "Settings", "Settings saved!")


def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')

    # Dark theme
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.WindowText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Base, QColor(25, 25, 25))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.ToolTipBase, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.ToolTipText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Text, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Button, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.ButtonText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.BrightText, Qt.GlobalColor.red)
    palette.setColor(QPalette.ColorRole.Link, QColor(42, 130, 218))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(42, 130, 218))
    palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.black)
    app.setPalette(palette)

    window = GBAUploaderApp()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
