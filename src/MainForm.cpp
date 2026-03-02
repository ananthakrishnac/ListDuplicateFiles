#include "..\include\MainForm.h"
#include "..\include\Logger.h"
#include "..\include\IconManager.h"
#include "..\include\UIHelpers.h"
#include "..\include\IconLoader.h"
#include "..\include\MemoryTracker.h"
#include "..\resources\resource.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// Macros for extracting coordinates from WM_SIZE lParam
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// Enable modern visual styles
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MainForm::MainForm() : hMainWindow(nullptr), fileScanner(std::make_unique<FileScanner>()),
                        dbManager(std::make_unique<DatabaseManager>()) {}

MainForm::~MainForm() {
    // Clean up background thread if still running
    if (pScanThread != nullptr) {
        // Signal thread to stop
        isScanRunning = false;
        LOG_INFO("Destructor: Stop signal sent to background thread");

        // Only join if thread is joinable (not detached)
        // If already detached (from OnStopScanClick), it will clean up on its own
        if (pScanThread->joinable()) {
            LOG_INFO("Destructor: Waiting for thread to complete...");
            // Use a timeout approach - don't block indefinitely
            // Thread should exit quickly after isScanRunning is set to false
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (pScanThread->joinable()) {
                pScanThread->join();
                LOG_INFO("Destructor: Thread joined successfully");
            }
        } else {
            LOG_INFO("Destructor: Thread already detached, skipping join");
        }

        delete pScanThread;
        pScanThread = nullptr;
    }

    if (hMainWindow) {
        DestroyWindow(hMainWindow);
    }
}

bool MainForm::Create() {
    LOG_INFO("MainForm::Create START");
    try {
        // Initialize Common Controls for modern UI
        INITCOMMONCONTROLSEX icce = { sizeof(icce), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icce);

        // Register window class with modern styling
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = GetModuleHandle(nullptr);

        // Load application icon from resources
        HICON hAppIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APPLICATIONICON));
        wcex.hIcon = hAppIcon ? hAppIcon : IconManager::GetFolderIcon();  // Use resource icon or fallback
        wcex.hIconSm = hAppIcon ? hAppIcon : IconManager::GetFolderIcon();  // Small icon for taskbar
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(UIColors::BACKGROUND);
        wcex.lpszClassName = L"FileExplorerWindow";

        if (!RegisterClassEx(&wcex)) {
            LOG_ERROR("RegisterClassEx failed");
            return false;
        }

        LOG_INFO("Window class registered");

        // Create main window with modern dimensions
        hMainWindow = CreateWindowExW(WS_EX_APPWINDOW, L"FileExplorerWindow",
                                     L"File Explorer - Database Manager",
                                     WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                     LayoutHelper::GetWindowWidth(), LayoutHelper::GetWindowHeight(), nullptr, nullptr,
                                     GetModuleHandle(nullptr), this);

        if (!hMainWindow) {
            LOG_ERROR("CreateWindowEx failed");
            return false;
        }

        LOG_INFO("Main window created");

        // Initialize database
        LOG_INFO("Initializing database");
        dbManager->InitializeDatabase("file_metadata.db");
        LOG_INFO("Database initialized");

        // Create controls
        LOG_INFO("Creating controls");
        CreateControls();
        LOG_INFO("Controls created");

        ShowWindow(hMainWindow, SW_SHOW);
        UpdateWindow(hMainWindow);

        LOG_INFO("MainForm::Create COMPLETE");
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("MainForm::Create exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_ERROR("MainForm::Create unknown exception");
        return false;
    }
}

void MainForm::CreateControls() {
    LOG_INFO("CreateControls START");

    RECT clientRect;
    GetClientRect(hMainWindow, &clientRect);
    int windowWidth = clientRect.right - clientRect.left;
    int windowHeight = clientRect.bottom - clientRect.top;

    OnWindowResize(windowWidth, windowHeight);
}

void MainForm::OnWindowResize(int width, int height) {
    LOG_DEBUG("OnWindowResize: width=" + std::to_string(width) + ", height=" + std::to_string(height));

    int xMargin = LayoutHelper::MARGIN;
    int yMargin = LayoutHelper::MARGIN;
    int contentWidth = width - (2 * xMargin);
    int controlHeight = LayoutHelper::CONTROL_HEIGHT;
    int spacing = LayoutHelper::SPACING;
    int buttonHeight = LayoutHelper::BUTTON_HEIGHT;
    int currentY = yMargin;

    // ===== ROW 1: Folder Path =====
    // Label with modern styling
    if (!hPathEdit) {  // Only create once
        HWND hPathLabel = CreateWindowExW(0, L"STATIC", L"Folder Path:",
                                          WS_CHILD | WS_VISIBLE,
                                          xMargin, currentY, 120, controlHeight, hMainWindow, nullptr,
                                          GetModuleHandle(nullptr), nullptr);
        LabelStyler::SetSectionTitle(hPathLabel);
    }

    // Path input - takes most of the space, button on the right
    int browseButtonWidth = 95;
    int pathInputWidth = contentWidth - browseButtonWidth - 10;  // 10px gap

    if (!hPathEdit) {
        hPathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  xMargin + 120, currentY, pathInputWidth - 120, controlHeight, hMainWindow, nullptr,
                                  GetModuleHandle(nullptr), nullptr);
        SendMessage(hPathEdit, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
    } else {
        MoveWindow(hPathEdit, xMargin + 120, currentY, pathInputWidth - 120, controlHeight, TRUE);
    }

    // Browse button on the right - displays opened_folder icon with "Browse" text
    if (!hBrowseButton) {
        hBrowseButton = CreateWindowExW(0, L"BUTTON", L"Browse",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       xMargin + pathInputWidth, currentY, browseButtonWidth, controlHeight,
                                       hMainWindow, (HMENU)1001, GetModuleHandle(nullptr), nullptr);
        ButtonStyler::SetSecondaryStyle(hBrowseButton);

        // Load opened_folder icon from file and display on button
        HICON hIcon = (HICON)LoadImageW(nullptr, L"icons\\opened_folder.ico",
                                         IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        if (hIcon) {
            // Use BCM_SETIMAGELIST to show both icon and text correctly in modern UI
            HIMAGELIST hImgList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
            ImageList_AddIcon(hImgList, hIcon);
            
            BUTTON_IMAGELIST bil = { 0 };
            bil.himl = hImgList;
            bil.margin.left = 4;
            bil.margin.right = 4;
            bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
            
            SendMessage(hBrowseButton, BCM_SETIMAGELIST, 0, (LPARAM)&bil);
            // DestroyIcon(hIcon); // hImgList takes a copy, but we should be careful with HICON lifetime if it's from LoadImage
        }
    } else {
        MoveWindow(hBrowseButton, xMargin + pathInputWidth, currentY, browseButtonWidth, controlHeight, TRUE);
    }

    currentY += spacing;

    // ===== ROW 2: File Extensions =====
    if (!hFileTypeEdit) {
        HWND hExtLabel = CreateWindowExW(0, L"STATIC", L"File Types (comma-separated):",
                                         WS_CHILD | WS_VISIBLE,
                                         xMargin, currentY, 300, controlHeight, hMainWindow, nullptr,
                                         GetModuleHandle(nullptr), nullptr);
        LabelStyler::SetSectionTitle(hExtLabel);
    }

    if (!hFileTypeEdit) {
        hFileTypeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"jpg,png,pdf,docx,txt",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       xMargin + 300, currentY, contentWidth - 300, controlHeight, hMainWindow, nullptr,
                                       GetModuleHandle(nullptr), nullptr);
        SendMessage(hFileTypeEdit, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
    } else {
        MoveWindow(hFileTypeEdit, xMargin + 300, currentY, contentWidth - 300, controlHeight, TRUE);
    }

    currentY += spacing;

    // ===== ROW 3: Include Subdirectories Checkbox =====
    if (!hIncludeSubdirCheckbox) {
        hIncludeSubdirCheckbox = CreateWindowExW(0, L"BUTTON", L"\u2713  Include Subdirectories", // \u2713 is CHECK MARK (✓)
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                               xMargin, currentY, 350, controlHeight, hMainWindow,
                                               (HMENU)1002, GetModuleHandle(nullptr), nullptr);
        LabelStyler::SetNormalLabel(hIncludeSubdirCheckbox);
        SendMessage(hIncludeSubdirCheckbox, BM_SETCHECK, BST_CHECKED, 0);
    } else {
        MoveWindow(hIncludeSubdirCheckbox, xMargin, currentY, 350, controlHeight, TRUE);
    }

    currentY += spacing;

    // ===== ROW 4: Buttons (Scan and Find Duplicates side by side) =====
    int buttonWidth = (contentWidth - 10) / 2;  // Two buttons with 10px gap

    if (!hScanButton) {
        hScanButton = CreateWindowExW(0, L"BUTTON", L"SCAN DIRECTORY",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    xMargin, currentY, buttonWidth, buttonHeight, hMainWindow,
                                    (HMENU)1000, GetModuleHandle(nullptr), nullptr);
        ButtonStyler::SetPrimaryStyle(hScanButton);

        // Load search icon from file and display on button with text
        HICON hIcon = (HICON)LoadImageW(nullptr, L"icons\\search.ico",
                                         IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        if (hIcon) {
            // Use BCM_SETIMAGELIST to show both icon and text correctly in modern UI
            HIMAGELIST hImgList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
            ImageList_AddIcon(hImgList, hIcon);

            BUTTON_IMAGELIST bil = { 0 };
            bil.himl = hImgList;
            bil.margin.left = 4;
            bil.margin.right = 4;
            bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;

            SendMessage(hScanButton, BCM_SETIMAGELIST, 0, (LPARAM)&bil);
        }
    } else {
        MoveWindow(hScanButton, xMargin, currentY, buttonWidth, buttonHeight, TRUE);
    }

    if (!hFindDuplicatesButton) {
        hFindDuplicatesButton = CreateWindowEx(0, L"BUTTON", L"FIND DUPLICATES",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                              xMargin + buttonWidth + 10, currentY, buttonWidth, buttonHeight, hMainWindow,
                                              (HMENU)1003, GetModuleHandle(nullptr), nullptr);
        ButtonStyler::SetSecondaryStyle(hFindDuplicatesButton);

        // Load answers icon from file and display on button with text
        HICON hIcon = (HICON)LoadImageW(nullptr, L"icons\\answers.ico",
                                         IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        if (hIcon) {
            // Use BCM_SETIMAGELIST to show both icon and text correctly in modern UI
            HIMAGELIST hImgList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
            ImageList_AddIcon(hImgList, hIcon);

            BUTTON_IMAGELIST bil = { 0 };
            bil.himl = hImgList;
            bil.margin.left = 4;
            bil.margin.right = 4;
            bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;

            SendMessage(hFindDuplicatesButton, BCM_SETIMAGELIST, 0, (LPARAM)&bil);
        }
    } else {
        MoveWindow(hFindDuplicatesButton, xMargin + buttonWidth + 10, currentY, buttonWidth, buttonHeight, TRUE);
    }

    currentY += buttonHeight + spacing;

    // ===== ROW 5: Results Label =====
    HWND hResultsLabel = GetDlgItem(hMainWindow, 2000);  // Try to find existing label
    if (!hResultsLabel) {
        hResultsLabel = CreateWindowExW(0, L"STATIC", L"\U0001F4CB  Results & Details:", // \U0001F4CB is CLIPBOARD (📋)
                                       WS_CHILD | WS_VISIBLE,
                                       xMargin, currentY, 250, controlHeight, hMainWindow,
                                       (HMENU)2000, GetModuleHandle(nullptr), nullptr);
        LabelStyler::SetHeaderFont(hResultsLabel);
    } else {
        MoveWindow(hResultsLabel, xMargin, currentY, 250, controlHeight, TRUE);
    }

    currentY += spacing;

    // ===== ROW 6: Results List (takes remaining space) =====
    int resultsHeight = height - currentY - xMargin - controlHeight - 10;  // Leave room for status bar

    if (!hResultsList) {
        hResultsList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                                     LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
                                     xMargin, currentY, contentWidth, resultsHeight, hMainWindow, nullptr,
                                     GetModuleHandle(nullptr), nullptr);
        SendMessage(hResultsList, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
    } else {
        MoveWindow(hResultsList, xMargin, currentY, contentWidth, resultsHeight, TRUE);
    }

    currentY += resultsHeight + spacing;

    // ===== ROW 7: Status Bar (fixed at bottom) =====
    if (!hStatusBar) {
        hStatusBar = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"\u2713 Ready", // \u2713 is CHECK MARK (✓)
                                   WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                                   xMargin, height - controlHeight - xMargin, contentWidth, controlHeight,
                                   hMainWindow, nullptr, GetModuleHandle(nullptr), nullptr);
        LabelStyler::SetNormalLabel(hStatusBar);
    } else {
        MoveWindow(hStatusBar, xMargin, height - controlHeight - xMargin, contentWidth, controlHeight, TRUE);
    }

    LOG_DEBUG("OnWindowResize COMPLETE");
}

LRESULT CALLBACK MainForm::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainForm* pThis = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<MainForm*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = reinterpret_cast<MainForm*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT MainForm::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == 1000) {  // Scan button - or Stop Scan if already running
                if (isScanRunning) {
                    OnStopScanClick();  // Button shows "Stop Scan" when scanning
                } else {
                    OnScanClick();      // Button shows "Scan" when not scanning
                }
            } else if (wmId == 1001) {  // Browse button
                OnBrowseClick();
            } else if (wmId == 1003) {  // Find Duplicates button
                OnFindDuplicatesClick();
            }
            break;
        }
        case WM_SIZE: {
            int width = GET_X_LPARAM(lParam);
            int height = GET_Y_LPARAM(lParam);
            OnWindowResize(width, height);
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hMainWindow, msg, wParam, lParam);
    }
    return 0;
}

void MainForm::OnBrowseClick() {
    if (!hPathEdit) return;

    BROWSEINFO bi = {};
    bi.hwndOwner = hMainWindow;
    bi.lpszTitle = L"Select a folder to scan";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl != nullptr) {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            SetWindowTextW(hPathEdit, path);
        }
        CoTaskMemFree(pidl);
    }
}

void MainForm::OnScanClick() {
    LOG_INFO("OnScanClick START");
    try {
        // Prevent multiple concurrent scans
        if (isScanRunning) {
            LOG_WARNING("Scan already in progress, ignoring new scan request");
            return;
        }

        if (!hPathEdit || !hFileTypeEdit || !hResultsList) {
            LOG_ERROR("OnScanClick - Control handles are null");
            return;
        }

        // Check if resuming from stopped scan
        std::string path;
        std::string extStr;
        bool isResume = false;

        if (shouldResume && !lastScanPath.empty()) {
            LOG_INFO("OnScanClick: Resuming previous scan from: " + lastScanPath);
            path = lastScanPath;
            isResume = true;
        } else {
            // Normal scan - get path from UI
            WCHAR pathBuf[MAX_PATH];
            GetWindowText(hPathEdit, pathBuf, MAX_PATH);
            LOG_DEBUG("Path entered: ");

            // Convert WCHAR path to regular string
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, pathBuf, -1, nullptr, 0, nullptr, nullptr);
            if (size_needed <= 0) {
                LOG_ERROR("WideCharToMultiByte size calculation failed for path");
                return;
            }

            path.resize(size_needed - 1);
            if (WideCharToMultiByte(CP_UTF8, 0, pathBuf, -1, &path[0], size_needed, nullptr, nullptr) <= 0) {
                LOG_ERROR("WideCharToMultiByte conversion failed for path");
                return;
            }
        }

        // Get extensions from UI (same for resume and normal scan)
        WCHAR extBuf[256];
        GetWindowText(hFileTypeEdit, extBuf, 256);
        LOG_DEBUG("Extensions entered");

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, extBuf, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed <= 0) {
            LOG_ERROR("WideCharToMultiByte size calculation failed for extensions");
            return;
        }

        extStr.resize(size_needed - 1);
        if (WideCharToMultiByte(CP_UTF8, 0, extBuf, -1, &extStr[0], size_needed, nullptr, nullptr) <= 0) {
            LOG_ERROR("WideCharToMultiByte conversion failed for extensions");
            return;
        }

        if (isResume) {
            shouldResume = false;  // Reset resume flag after using it
            LOG_INFO("Resume mode reset");
        }

        LOG_INFO("Converted path: " + path);
        LOG_INFO("Extensions: " + extStr);

        // Check include subdirectories
        bool includeSubdirs = false;
        if (hIncludeSubdirCheckbox) {
            includeSubdirs = SendMessage(hIncludeSubdirCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            LOG_DEBUG("Include subdirectories: " + std::string(includeSubdirs ? "Yes" : "No"));
        }

        // Parse extensions
        std::vector<std::string> extensions;
        std::stringstream ss(extStr);
        std::string ext;
        int extCount = 0;
        while (std::getline(ss, ext, ',')) {
            // Trim whitespace
            size_t start = ext.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                ext.erase(0, start);
            }
            size_t end = ext.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) {
                ext.erase(end + 1);
            }
            if (!ext.empty()) {
                extensions.push_back(ext);
                extCount++;
                LOG_DEBUG("Added extension: " + ext);
            }
        }

        LOG_INFO("Extension count: " + std::to_string(extCount));

        // Capture path for resume functionality
        lastScanPath = path;
        LOG_INFO("Saved scan path for resume: " + lastScanPath);

        UpdateStatusBar(u8"\U0001F50D  Scanning...  Please wait"); // \U0001F50D is LEFT-POINTING MAGNIFYING GLASS (🔍)
        SendMessage(hResultsList, LB_RESETCONTENT, 0, 0);

        // Show initial message
        std::wstring separator = Utf8ToUtf16(u8"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501");
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\U0001F50D  Scanning in progress...").c_str()); // \U0001F50D is LEFT-POINTING MAGNIFYING GLASS (🔍)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F4C1  Path: ") + Utf8ToUtf16(path)).c_str()); // \U0001F4C1 is OPEN FILE FOLDER (📁)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F4C4  Types: ") + Utf8ToUtf16(extStr)).c_str()); // \U0001F4C4 is DOCUMENT (📄)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)L"");

        LOG_INFO("Starting background scan thread");

        // Stop Scan Implementation - Initialize stop flag and update button
        isScanRunning = true;
        filesScannedBeforeStop = 0;
        LOG_INFO("Scan mode: RUNNING - Stop button active");

        // Change button text to "Stop Scan"
        if (hScanButton) {
            SetWindowTextW(hScanButton, L"\u23F9  STOP"); // \u23F9 is RECTANGULAR STOP (⏹)
        }

        // Scan in background thread
        pScanThread = new std::thread([this, path, extensions, includeSubdirs]() {
            try {
                LOG_INFO("Background thread: Starting scan");
                MEMORY_START();

                // Check if scan was cancelled before thread started
                if (!isScanRunning) {
                    LOG_INFO("Scan cancelled before start");
                    return;
                }

                // Set progress callback to update status bar with current directory
                fileScanner->SetProgressCallback([this](const std::string& currentPath) {
                    std::string status = "[SCANNING] " + currentPath;
                    UpdateStatusBar(status);
                });

                // Set batch results callback - called for every 100 files found
                int totalInserted = 0;
                int batchNum = 0;
                fileScanner->SetResultsBatchCallback([this, &totalInserted, &batchNum](std::vector<FileMetadata>& batch) {
                    // Stop Scan Check - Break if user clicked stop
                    if (!isScanRunning) {
                        filesScannedBeforeStop = totalInserted;
                        LOG_INFO("Stop requested during streaming - stopping at " + std::to_string(totalInserted) + " files");
                        batch.clear();
                        return;
                    }

                    // Track batch memory
                    size_t batchCapacity = batch.capacity() * sizeof(FileMetadata);
                    MEMORY_TRACK_ALLOC("MainForm::batch_vector", batchCapacity);

                    batchNum++;
                    int batchInserted = dbManager->InsertFileMetadataBatch(batch);
                    totalInserted += batchInserted;

                    LOG_INFO("Background thread: Batch " + std::to_string(batchNum) +
                             " processed - Inserted " + std::to_string(batchInserted) + " new records");

                    // Update status bar with progress
                    std::string status = "[STORING] Batch " + std::to_string(batchNum) +
                                        " - " + std::to_string(totalInserted) + " total inserted";
                    UpdateStatusBar(status);

                    // Batch is cleared automatically after callback returns
                    MEMORY_TRACK_FREE("MainForm::batch_vector", batchCapacity);
                });

                // Pass stop flag to scanner for early termination on user request
                fileScanner->SetStopFlag(&isScanRunning);
                LOG_INFO("Stop flag set in FileScanner");

                // Stream-based scan: processes files in 100-file batches without accumulating all in memory
                LOG_INFO("Background thread: Starting streaming scan");
                int totalScanned = fileScanner->ScanDirectoryStreaming(path, extensions, includeSubdirs);
                LOG_INFO("Background thread: Streaming scan complete - " + std::to_string(totalScanned) + " files found");

                try {
                    // Final flush to ensure all data is written
                    LOG_INFO("Background thread: Final database flush");
                    dbManager->ExecuteSQL("PRAGMA synchronous = FULL;");

                    // Store total inserted count instead of loading all results back into memory
                    // This avoids the 1GB memory spike from loading 47K files just for display
                    lastScanResults.clear();
                    lastScanResults.shrink_to_fit();
                    LOG_INFO("Background thread: Scan complete - " + std::to_string(totalInserted) + " files inserted");

                } catch (const std::exception& ex) {
                    LOG_ERROR("Database final flush failed: " + std::string(ex.what()));
                } catch (...) {
                    LOG_ERROR("Database final flush failed with unknown exception");
                }

                LOG_INFO("Background thread: All batches processed - Total inserted: " + std::to_string(totalInserted));

                LOG_INFO("Background thread: Calling OnScanComplete");
                OnScanComplete();

                // Print memory report AFTER cleanup is complete
                MEMORY_REPORT();
                LOG_INFO("Background thread: Complete");
            } catch (const std::exception& ex) {
                LOG_ERROR("Background thread exception: " + std::string(ex.what()));
            } catch (...) {
                LOG_ERROR("Background thread unknown exception");
            }
        });
        // Note: Thread is stored in pScanThread for potential join/cleanup in OnStopScanClick()

        LOG_INFO("OnScanClick COMPLETE");
    } catch (const std::exception& ex) {
        LOG_ERROR("OnScanClick exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("OnScanClick unknown exception");
    }
}

void MainForm::OnStopScanClick() {
    LOG_INFO("OnStopScanClick START");
    try {
        // Step 1: Signal scanner to stop (this will cause background thread to exit gracefully)
        isScanRunning = false;
        shouldResume = true;  // Mark that resume is available
        LOG_INFO("Stop signal sent to scanner - isScanRunning = false");

        // Step 2: Detach thread instead of join to avoid UI hang
        // Background thread will clean up when it finishes, not blocking UI
        if (pScanThread != nullptr) {
            if (pScanThread->joinable()) {
                LOG_INFO("Detaching background thread (non-blocking)");
                pScanThread->detach();
            }
            delete pScanThread;
            pScanThread = nullptr;
        }

        // Step 3: Change button text to "Resume Scan" immediately
        if (hScanButton) {
            SetWindowTextW(hScanButton, L"Resume Scan");
            LOG_INFO("Button changed to 'Resume Scan'");
        }

        // Step 4: Update status bar
        std::string status = u8"\u23F8  Paused  |  " + std::to_string(filesScannedBeforeStop) + " files scanned"; // \u23F8 is PAUSE BUTTON (⏸)
        UpdateStatusBar(status);
        LOG_INFO("Status updated: " + status);

        // Step 5: Display stop message in ListBox
        std::wstring separator = Utf8ToUtf16(u8"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501");
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\u23F8  Scan Paused").c_str()); // \u23F8 is PAUSE BUTTON (⏸)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F4CA  Files Processed: ") + std::to_wstring(filesScannedBeforeStop)).c_str()); // \U0001F4CA is BAR CHART (📊)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\U0001F4BE  Click 'RESUME SCAN' to continue from this point").c_str()); // \U0001F4BE is FLOPPY DISK (💾)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());

        LOG_INFO("OnStopScanClick COMPLETE");
    } catch (const std::exception& ex) {
        LOG_ERROR("OnStopScanClick exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("OnStopScanClick unknown exception");
    }
}

void MainForm::OnScanComplete() {
    LOG_INFO("OnScanComplete START");
    try {
        SendMessage(hResultsList, LB_RESETCONTENT, 0, 0);
        LOG_INFO("Results list cleared");

        // Note: lastScanResults is now empty (not loaded from DB to save memory)
        // Show completion message with file count from database
        int totalFiles = dbManager->GetTotalFileCount();
        LOG_INFO("OnScanComplete: Scan finished with " + std::to_string(totalFiles) + " files");

        std::string status = u8"\u2713  Scan Complete  |  " + std::to_string(totalFiles) + " files stored"; // \u2713 is CHECK MARK (✓)
        UpdateStatusBar(status);

        // Add beautiful summary to the ListBox
        std::wstring separator = Utf8ToUtf16(u8"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501");
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\u2713  Scan Complete!").c_str()); // \u2713 is CHECK MARK (✓)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F4CA  Total Files: ") + std::to_wstring(totalFiles)).c_str()); // \U0001F4CA is BAR CHART (📊)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\U0001F4BE  All files stored in database").c_str()); // \U0001F4BE is FLOPPY DISK (💾)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\U0001F50E  Click 'FIND DUPLICATES' to identify copies").c_str()); // \U0001F50E is RIGHT-POINTING MAGNIFYING GLASS (🔎)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());

        // Signal that scan is no longer running
        isScanRunning = false;
        shouldResume = false;  // Reset resume flag on completion
        LOG_INFO("Scan completed - isScanRunning set to false");

        // Restore button to "Scan" (normal completion, not stopped)
        if (hScanButton) {
            SetWindowTextW(hScanButton, L"\U0001F50D  SCAN DIRECTORY"); // \U0001F50D is LEFT-POINTING MAGNIFYING GLASS (🔍)
            LOG_INFO("Button restored to 'SCAN'");
        }

        LOG_INFO("OnScanComplete COMPLETE");
    } catch (const std::exception& ex) {
        LOG_ERROR("OnScanComplete exception: " + std::string(ex.what()));
        isScanRunning = false;
        shouldResume = false;
        if (hScanButton) {
            SetWindowText(hScanButton, L"Scan");
        }
    } catch (...) {
        LOG_ERROR("OnScanComplete unknown exception");
        isScanRunning = false;
        shouldResume = false;
        if (hScanButton) {
            SetWindowText(hScanButton, L"Scan");
        }
    }
}

void MainForm::UpdateStatusBar(const std::string& message) {
    if (!hStatusBar) {
        LOG_WARNING("UpdateStatusBar - Status bar handle is null");
        return;
    }
    try {
        std::wstring wmessage = Utf8ToUtf16(message);
        SetWindowTextW(hStatusBar, wmessage.c_str());
        LOG_DEBUG("Status bar updated: " + message);
    } catch (const std::exception& ex) {
        LOG_ERROR("UpdateStatusBar exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("UpdateStatusBar unknown exception");
    }
}

void MainForm::DisplayResults(const std::vector<FileMetadata>& files) {
    LOG_INFO("DisplayResults START - Files: " + std::to_string(files.size()));
    try {
        int addedCount = 0;
        for (const auto& file : files) {
            try {
                // Format file size in KB/MB for readability
                std::string sizeStr;
                if (file.fileSize < 1024) {
                    sizeStr = std::to_string(file.fileSize) + "B";
                } else if (file.fileSize < 1024 * 1024) {
                    sizeStr = std::to_string(file.fileSize / 1024) + "KB";
                } else {
                    sizeStr = std::to_string(file.fileSize / (1024 * 1024)) + "MB";
                }

                std::string displayStr = file.filename + "  [" + sizeStr +
                                         "]  (" + file.fileType + ")";
                std::wstring wdisplay = Utf8ToUtf16(displayStr);
                SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)wdisplay.c_str());
                addedCount++;
            } catch (const std::exception& ex) {
                LOG_ERROR("Error displaying file: " + std::string(ex.what()));
            } catch (...) {
                LOG_ERROR("Unknown exception displaying file");
            }
        }
        LOG_INFO("DisplayResults COMPLETE - Added " + std::to_string(addedCount) + " items");
    } catch (const std::exception& ex) {
        LOG_ERROR("DisplayResults exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("DisplayResults unknown exception");
    }
}

void MainForm::OnFindDuplicatesClick() {
    LOG_INFO("OnFindDuplicatesClick START");
    try {
        if (!dbManager) {
            UpdateStatusBar("[ERROR] Database manager not available");
            LOG_ERROR("OnFindDuplicatesClick - Database manager is null");
            return;
        }

        UpdateStatusBar(u8"\U0001F50E  Searching for duplicates..."); // \U0001F50E is RIGHT-POINTING MAGNIFYING GLASS (🔎)
        LOG_INFO("Finding duplicates from database");

        // Find all duplicate files grouped by hash
        auto duplicateGroups = dbManager->FindDuplicates();

        if (duplicateGroups.empty()) {
            UpdateStatusBar(u8"\u2713  No duplicate files found"); // \u2713 is CHECK MARK (✓)
            SendMessage(hResultsList, LB_RESETCONTENT, 0, 0);
            std::wstring separator = Utf8ToUtf16(u8"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501");
            SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());
            SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\u2713  All files are unique!").c_str()); // \u2713 is CHECK MARK (✓)
            SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)Utf8ToUtf16(u8"\U0001F389  No duplicate files found in the database").c_str()); // \U0001F389 is PARTY POPPER (🎉)
            SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)separator.c_str());
            LOG_INFO("No duplicates found");
            return;
        }

        LOG_INFO("Found " + std::to_string(duplicateGroups.size()) + " duplicate groups");
        DisplayDuplicates(duplicateGroups);

        std::string status = u8"\u2713  Found " + std::to_string(duplicateGroups.size()) + " duplicate groups"; // \u2713 is CHECK MARK (✓)
        UpdateStatusBar(status);
        LOG_INFO("OnFindDuplicatesClick COMPLETE");

    } catch (const std::exception& ex) {
        LOG_ERROR("OnFindDuplicatesClick exception: " + std::string(ex.what()));
        UpdateStatusBar(u8"\u274C  Error during duplicate search"); // \u274C is CROSS MARK (❌)
    } catch (...) {
        LOG_ERROR("OnFindDuplicatesClick unknown exception");
        UpdateStatusBar(u8"\u274C  Unknown error during duplicate search"); // \u274C is CROSS MARK (❌)
    }
}

void MainForm::DisplayDuplicates(const std::vector<std::vector<FileMetadata>>& duplicateGroups) {
    LOG_INFO("DisplayDuplicates START - Groups: " + std::to_string(duplicateGroups.size()));
    try {
        // Clear the results list
        SendMessage(hResultsList, LB_RESETCONTENT, 0, 0);

        if (duplicateGroups.empty()) {
            SendMessage(hResultsList, LB_ADDSTRING, 0, (LPARAM)L"[No duplicates]");
            LOG_INFO("DisplayDuplicates - No groups");
            return;
        }

        int groupNum = 0;
        int totalDuplicates = 0;

        // Add header
        std::wstring headerSep = Utf8ToUtf16(u8"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501");
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)headerSep.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F50E  DUPLICATE FILES FOUND: ") + std::to_wstring(duplicateGroups.size()) + L" groups").c_str()); // \U0001F50E is RIGHT-POINTING MAGNIFYING GLASS (🔎)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)headerSep.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)L"");

        // Display each duplicate group
        for (const auto& group : duplicateGroups) {
            if (group.empty()) continue;

            groupNum++;
            totalDuplicates += static_cast<int>(group.size());

            // Get the common filename and hash
            const auto& firstFile = group[0];
            std::string filename = firstFile.filename;
            std::string hashVal = firstFile.hashVal;

            // Format: "📁 Group N: filename - X copies (HASH_PREFIX)"
            std::string headerStr = u8"\U0001F4C1 Group " + std::to_string(groupNum) + ": " + filename + // \U0001F4C1 is OPEN FILE FOLDER (📁)
                                   " (" + std::to_string(group.size()) + " copies) [" +
                                   (hashVal.length() > 8 ? hashVal.substr(0, 8) : hashVal) + "]";
            std::wstring wheader = Utf8ToUtf16(headerStr);
            SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)wheader.c_str());

            // Display each file in the group with its full path
            for (size_t i = 0; i < group.size(); i++) {
                const auto& file = group[i];

                // Format file size
                std::string sizeStr;
                if (file.fileSize < 1024) {
                    sizeStr = std::to_string(file.fileSize) + "B";
                } else if (file.fileSize < 1024 * 1024) {
                    sizeStr = std::to_string(file.fileSize / 1024) + "KB";
                } else if (file.fileSize < 1024 * 1024 * 1024) {
                    sizeStr = std::to_string(file.fileSize / (1024 * 1024)) + "MB";
                } else {
                    sizeStr = std::to_string(file.fileSize / (1024 * 1024 * 1024)) + "GB";
                }

                // Format: "  ▶ [N] full_path  [size]"
                std::string pathStr = u8"   \u25B6 " + std::to_string(i + 1) + ".  " + file.fullPath + // \u25B6 is BLACK RIGHT-POINTING TRIANGLE (▶)
                                     "  (" + sizeStr + ")";
                std::wstring wpath = Utf8ToUtf16(pathStr);
                SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)wpath.c_str());

                LOG_INFO("Duplicate " + std::to_string(groupNum) + "." + std::to_string(i + 1) +
                        ": " + file.fullPath + " (" + sizeStr + ")");
            }

            // Add blank line for separation
            SendMessage(hResultsList, LB_ADDSTRING, 0, (LPARAM)L"");
        }

        // Add footer
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)headerSep.c_str());
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)(Utf8ToUtf16(u8"\U0001F4A1  Total: ") + std::to_wstring(totalDuplicates) + L" duplicate files across " + std::to_wstring(groupNum) + L" groups").c_str()); // \U0001F4A1 is ELECTRIC LIGHT BULB (💡)
        SendMessageW(hResultsList, LB_ADDSTRING, 0, (LPARAM)headerSep.c_str());

        LOG_INFO("DisplayDuplicates COMPLETE - " + std::to_string(groupNum) + " groups, " +
                std::to_string(totalDuplicates) + " total duplicate files");

    } catch (const std::exception& ex) {
        LOG_ERROR("DisplayDuplicates exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("DisplayDuplicates unknown exception");
    }
}

int MainForm::Run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

std::wstring MainForm::Utf8ToUtf16(const std::string& utf8Str) {
    if (utf8Str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
