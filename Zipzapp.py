import os
import zipfile
import threading
import sys
from PyQt5.QtWidgets import QApplication, QSystemTrayIcon, QMenu, QWidgetAction, QWidget, QVBoxLayout, QAction, QFileDialog, QListWidget, QListWidgetItem
from PyQt5.QtGui import QIcon
from PyQt5.QtCore import QCoreApplication, Qt, QRect, QTimer
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from PyQt5.QtCore import QObject, pyqtSignal

class ProgressSignalHandler(QObject):
    extraction_started_signal = pyqtSignal()
    extraction_finished_signal = pyqtSignal()

class ZipExtractorHandler(FileSystemEventHandler):
    def __init__(self, progress_handler):
        super().__init__()
        self.progress_handler = progress_handler

    def on_created(self, event):
        # This method is called when a file is created in the monitored directory
        if not event.is_directory and event.src_path.endswith('.zip'):
            print(f"ZIP file created in monitored folder: {event.src_path}")
            try:
                self.extract_zip(event.src_path)
            except PermissionError as e:
                print(f"PermissionError: {e}. Cannot extract the ZIP file.")

    def extract_zip(self, zip_path):
        self.progress_handler.extraction_started_signal.emit()  # Signal that extraction has started

        # Extract the zip file to the same directory
        folder_path = zip_path.rsplit('.', 1)[0]
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(folder_path)

        os.remove(zip_path)  # Remove the zip file after extraction

        self.progress_handler.extraction_finished_signal.emit()  # Signal that extraction has finished

class SystemTrayApp(QSystemTrayIcon):
    def __init__(self, icon, busy_icon, parent=None):
        super(SystemTrayApp, self).__init__(icon, parent)
        self.setToolTip('System Tray App')
        self.busy_icon = busy_icon  # Store the busy icon

        # Create the context menu
        self.menu = QMenu(parent)

        # Watch Folders Action
        self.watchFoldersAction = QWidgetAction(self.menu)
        self.watchFoldersWidget = QWidget(self.menu)
        self.watchFoldersLayout = QVBoxLayout(self.watchFoldersWidget)

        # List widget for displaying selected watch folders
        self.watchFolderListWidget = QListWidget(self.watchFoldersWidget)
        self.watchFoldersLayout.addWidget(self.watchFolderListWidget)

        # Add selected watch folders to the list
        self.watchFolderList = []  # List to store selected watch folders
        self.loadWatchFolders()  # Load saved watch folders

        # Add a button to add new watch folders
        self.addButton = QAction("Add Watch Folder", self.menu)
        self.addButton.triggered.connect(self.addWatchFolder)
        self.menu.addAction(self.addButton)

        # Add a button to remove selected watch folders
        self.removeButton = QAction("Remove Watch Folder", self.menu)
        self.removeButton.triggered.connect(self.removeSelectedWatchFolder)
        self.menu.addAction(self.removeButton)

        self.watchFoldersAction.setDefaultWidget(self.watchFoldersWidget)
        self.menu.addAction(self.watchFoldersAction)

        # Quit Action
        self.quitAction = QAction("Quit", self.menu)
        self.quitAction.triggered.connect(QCoreApplication.quit)
        self.menu.addAction(self.quitAction)

        # Handling clicks on the system tray icon
        self.activated.connect(self.onTrayIconActivated)

        # Create and show the progress icon
        self.progress_icon = QIcon("active.png")  # Use your custom busy icon path
        self.default_icon = QIcon("icon.png")  # Use your default icon path
        self.setIcon(self.default_icon)

        # Create a ProgressSignalHandler instance to handle extraction signals
        self.progress_signal_handler = ProgressSignalHandler()

        # Create a ZipExtractorHandler instance and pass the progress signal handler
        self.extractor_handler = ZipExtractorHandler(self.progress_signal_handler)

        # Connect the extraction signals to change_icon functions
        self.progress_signal_handler.extraction_started_signal.connect(self.change_icon_extraction_started)
        self.progress_signal_handler.extraction_finished_signal.connect(self.change_icon_extraction_finished)

    def onTrayIconActivated(self, reason):
        # Get the geometry of the system tray icon
        tray_icon_geometry = self.geometry()

        # Calculate the horizontal position for the context menu (to the left of the tray icon)
        menu_x = tray_icon_geometry.left() - self.menu.sizeHint().width()

        # Calculate the vertical position for the context menu (above the taskbar)
        taskbar_y = QApplication.desktop().availableGeometry().bottom() - self.menu.sizeHint().height()

        # Create a QRect for the menu position
        menu_position = QRect(menu_x, taskbar_y, self.menu.sizeHint().width(), self.menu.sizeHint().height())

        # Show the context menu at the specified position
        self.menu.popup(menu_position.topLeft())

    def change_icon_extraction_started(self):
        # Change the system tray icon to the busy icon when extraction starts
        self.setIcon(self.busy_icon)

    def change_icon_extraction_finished(self):
        # Change the system tray icon back to the default when extraction finishes
        self.setIcon(self.default_icon)

    def addWatchFolder(self):
        options = QFileDialog.Options()
        options |= QFileDialog.ReadOnly  # Optional: Set read-only mode for selected folders
        options |= QFileDialog.DirectoryOnly  # Optional: Allow only selecting directories

        default_folder = os.path.expanduser("~")  # Get the user's home directory as the default folder
        selected_folder = QFileDialog.getExistingDirectory(
            None,
            "Select Watch Folder",
            default_folder,
            options=options
        )

        if selected_folder:
            # Replace forward slashes with backslashes for consistency on Windows
            selected_folder = selected_folder.replace("/", "\\")
            
            # Add the selected watch folder to the list
            self.watchFolderList.append(selected_folder)
            self.saveWatchFolders()  # Save updated watch folders list
            self.updateWatchFolderListWidget()

    def removeSelectedWatchFolder(self):
        selected_items = self.watchFolderListWidget.selectedItems()
        for item in selected_items:
            folder = item.text()
            self.watchFolderList.remove(folder)
            self.watchFolderListWidget.takeItem(self.watchFolderListWidget.row(item))
        self.saveWatchFolders()  # Save updated watch folders list

    def updateWatchFolderListWidget(self):
        self.watchFolderListWidget.clear()
        for folder in self.watchFolderList:
            item = QListWidgetItem(folder)
            self.watchFolderListWidget.addItem(item)

    def saveWatchFolders(self):
        script_directory = os.path.dirname(os.path.abspath(__file__))
        watch_folders_file = os.path.join(script_directory, "watch_folders.txt")

        with open(watch_folders_file, "w") as file:
            for folder in self.watchFolderList:
                file.write(folder + "\n")

    def loadWatchFolders(self):
        script_directory = os.path.dirname(os.path.abspath(__file__))
        watch_folders_file = os.path.join(script_directory, "watch_folders.txt")

        if os.path.isfile(watch_folders_file):
            # Load watch folders from the file if it exists
            print(f"File '{watch_folders_file}' exists.")
            with open(watch_folders_file, "r") as file:
                self.watchFolderList = [line.strip() for line in file]
        else:
            print(f"File '{watch_folders_file}' does not exist or is empty. Creating the file...")
            # If the file doesn't exist or is empty, initialize with a default folder
            default_folder = os.path.expanduser("~\\Downloads")
            self.watchFolderList = [default_folder]

            # Create the file and write the default folder path
            with open(watch_folders_file, "w") as file:
                file.write(default_folder)

            print(f"File '{watch_folders_file}' has been successfully created with the default folder.")

        self.updateWatchFolderListWidget()

def start_monitoring(watch_folders, progress_signal_handler):
    event_handler = ZipExtractorHandler(progress_signal_handler)

    for watch_folder in watch_folders:
        observer = Observer()
        observer.schedule(event_handler, watch_folder, recursive=False)
        observer.start()

    try:
        while True:
            pass  # Keep the program running to continue monitoring
    except KeyboardInterrupt:
        for observer in observer_list:
            observer.stop()

    for observer in observer_list:
        observer.join()

def main():
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    trayIcon = SystemTrayApp(QIcon("icon.png"), QIcon("active.png"))  # Specify both icons
    trayIcon.show()

    # Start the watchdog observer in a separate thread
    threading.Thread(target=start_monitoring, args=(trayIcon.watchFolderList, trayIcon.progress_signal_handler), daemon=True).start()

    sys.exit(app.exec_())

if __name__ == '__main__':
    main()
