// src/main.cpp

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <iostream>
#include "WindowManager.h"
#include "FolderWatcher.h"
#include "Utilities.h"

// Global variables
HINSTANCE hInst;

// Function to get the Downloads folder path
std::wstring GetDownloadsFolder()
{
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    if (SUCCEEDED(hr))
    {
        std::wstring downloadsFolder(path);
        CoTaskMemFree(path);
        return downloadsFolder;
    }
    else
    {
        std::wcerr << L"Failed to get Downloads folder path." << std::endl;
        Log(L"Failed to get Downloads folder path.");
        return L"";
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    // Initialize COM library
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        std::wcerr << L"Failed to initialize COM library." << std::endl;
        return -1;
    }

    hInst = hInstance;

    // Get Downloads folder
    std::wstring downloadsFolder = GetDownloadsFolder();
    if (downloadsFolder.empty())
    {
        CoUninitialize();
        return -1;
    }

    // Initialize WindowManager
    WindowManager windowManager(hInstance, L"ZipZapClass", L"ZipZap");
    if (!windowManager.Initialize())
    {
        CoUninitialize();
        return -1;
    }

    // Initialize FolderWatcher
    FolderWatcher folderWatcher(downloadsFolder);
    folderWatcher.Start();

    // Set callback for exit
    windowManager.SetTrayIconCallback([&]() {
        folderWatcher.Stop();
        windowManager.ShowBalloonTip(L"ZipZap", L"Exiting ZipZap.");
        PostQuitMessage(0);
    });

    // Run the message loop
    windowManager.RunMessageLoop();

    // Wait for FolderWatcher to finish
    // Destructor of FolderWatcher will handle thread joining

    CoUninitialize();
    return 0;
}
