import sys
import os
import zipfile
import threading
from PyQt5.QtWidgets import QApplication, QSystemTrayIcon, QMenu
from PyQt5.QtGui import QIcon
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

class ZipExtractorHandler(FileSystemEventHandler):
    def on_created(self, event):
        # This method is called when a file is created in the monitored directory
        if not event.is_directory and event.src_path.endswith('.zip'):
            self.extract_zip(event.src_path)

    def extract_zip(self, zip_path):
        # Extract the zip file to the same directory without progress updates
        folder_path = zip_path.rsplit('.', 1)[0]
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(folder_path)
        os.remove(zip_path)  # Remove the zip file after extraction

class SystemTrayIcon(QSystemTrayIcon):
    def __init__(self, icon, parent=None):
        super(SystemTrayIcon, self).__init__(icon, parent)
        self.setToolTip('Zip Extractor App')

        menu = QMenu(parent)
        quit_action = menu.addAction('Quit')
        quit_action.triggered.connect(sys.exit)

        self.setContextMenu(menu)

def start_monitoring():
    path = os.path.expanduser('~/Downloads')  # Path to the Downloads folder
    event_handler = ZipExtractorHandler()
    observer = Observer()
    observer.schedule(event_handler, path, recursive=False)
    observer.start()
    observer.join()

def main():
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    tray_icon = SystemTrayIcon(QIcon("icon.png"))  # Ensure "icon.png" exists in your directory
    tray_icon.show()

    # Start the watchdog observer in a separate thread
    threading.Thread(target=start_monitoring, daemon=True).start()

    sys.exit(app.exec_())

if __name__ == '__main__':
    main()
