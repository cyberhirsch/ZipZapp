#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <iostream>
#include <libarchive/archive.h>
#include <libarchive/archive_entry.h>

#pragma comment(lib, "shell32.lib")

// Resource identifiers
#include "resource.h"

// Global variables
NOTIFYICONDATA nid = {};
HINSTANCE hInst;
std::wstring watchFolder;
HICON hIconDefault;
HICON hIconBusy;

// Custom message for updating the icon
#define WM_UPDATE_ICON (WM_APP + 1)

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitNotifyIcon(HWND hWnd);
void CleanupNotifyIcon();
void WatchFolder();
void UnpackArchive(const std::wstring& filePath);
void UpdateTrayIcon(HICON hIcon);
std::wstring GetDownloadsFolder();

std::wstring GetDownloadsFolder()
{
    PWSTR path = nullptr;
    SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    std::wstring downloadsFolder(path);
    CoTaskMemFree(path);
    return downloadsFolder;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    hInst = hInstance;
    watchFolder = GetDownloadsFolder();

    // Load icons
    hIconDefault = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ZIPZAP_ICON));
    hIconBusy = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ZIPZAP_BUSY));

    // Register window class
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInst;
    wcex.lpszClassName = L"ZipZapClass";
    RegisterClassEx(&wcex);

    // Create window
    HWND hWnd = CreateWindow(L"ZipZapClass", L"ZipZap", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInst, nullptr);

    // Initialize Notify Icon
    InitNotifyIcon(hWnd);

    // Start the folder watcher in a separate thread
    std::thread watcher(WatchFolder);
    watcher.detach();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupNotifyIcon();

    return (int)msg.wParam;
}

void InitNotifyIcon(HWND hWnd)
{
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1001;  // Arbitrary ID
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;  // Custom message

    // Use the default icon
    nid.hIcon = hIconDefault;

    wcscpy_s(nid.szTip, L"ZipZap - Auto Unpacker");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void CleanupNotifyIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &nid);

    // Destroy icons
    DestroyIcon(hIconDefault);
    DestroyIcon(hIconBusy);
}

void UpdateTrayIcon(HICON hIcon)
{
    // Post a message to the main window to update the icon
    PostMessage(nid.hWnd, WM_UPDATE_ICON, (WPARAM)hIcon, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_USER + 1)
    {
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            // Show context menu
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            UINT clicked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            if (clicked == 1)
            {
                PostQuitMessage(0);
            }
            DestroyMenu(hMenu);
        }
    }
    else if (message == WM_UPDATE_ICON)
    {
        // Update the tray icon
        nid.hIcon = (HICON)wParam;
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        return 0;
    }
    else if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void WatchFolder()
{
    HANDLE hDir = CreateFile(
        watchFolder.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,                         // security attributes
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,   // required for directories
        NULL);

    if (hDir == INVALID_HANDLE_VALUE)
    {
        std::wcerr << L"Failed to get handle to directory." << std::endl;
        return;
    }

    BYTE buffer[1024];
    DWORD bytesReturned;

    while (true)
    {
        if (ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned,
            NULL,
            NULL))
        {
            DWORD offset = 0;
            FILE_NOTIFY_INFORMATION* fni = nullptr;

            do
            {
                fni = (FILE_NOTIFY_INFORMATION*)((LPBYTE)buffer + offset);
                std::wstring fileName(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

                // Process the file
                std::wstring extension = fileName.substr(fileName.find_last_of(L".") + 1);
                if (extension == L"zip" || extension == L"7z" || extension == L"rar")
                {
                    std::wstring fullPath = watchFolder + L"\\" + fileName;
                    // Start unpacking in a new thread
                    std::thread unpacker(UnpackArchive, fullPath);
                    unpacker.detach();
                }

                offset += fni->NextEntryOffset;
            } while (fni->NextEntryOffset != 0);
        }
    }

    CloseHandle(hDir);
}

void UnpackArchive(const std::wstring& filePath)
{
    // Change icon to busy
    UpdateTrayIcon(hIconBusy);

    struct archive* a;
    struct archive* ext;
    struct archive_entry* entry;
    int flags;
    int r;

    // Select which attributes we want to restore
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    std::string filePathStr(filePath.begin(), filePath.end());

    if ((r = archive_read_open_filename(a, filePathStr.c_str(), 10240)))
    {
        std::cerr << "Could not open archive: " << filePathStr << std::endl;
        // Change icon back to default
        UpdateTrayIcon(hIconDefault);
        return;
    }

    while (true)
    {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(a) << std::endl;
        if (r < ARCHIVE_WARN)
            break;

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(ext) << std::endl;
        else if (archive_entry_size(entry) > 0)
        {
            const void* buff;
            size_t size;
            la_int64_t offset;

            while (true)
            {
                r = archive_read_data_block(a, &buff, &size, &offset);
                if (r == ARCHIVE_EOF)
                    break;
                if (r < ARCHIVE_OK)
                    std::cerr << archive_error_string(a) << std::endl;
                if (r < ARCHIVE_WARN)
                    break;

                r = archive_write_data_block(ext, buff, size, offset);
                if (r < ARCHIVE_OK)
                {
                    std::cerr << archive_error_string(ext) << std::endl;
                    break;
                }
            }
        }

        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(ext) << std::endl;
        if (r < ARCHIVE_WARN)
            break;
    }

    archive_read_close(a);
    archive_read_free(a);

    archive_write_close(ext);
    archive_write_free(ext);

    // Delete the archive after successful extraction
    if (DeleteFile(filePath.c_str()))
    {
        std::wcout << L"Deleted archive: " << filePath << std::endl;
    }
    else
    {
        std::wcerr << L"Failed to delete archive: " << filePath << std::endl;
    }

    // Change icon back to default
    UpdateTrayIcon(hIconDefault);
}
