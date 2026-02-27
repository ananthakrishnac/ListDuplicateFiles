#pragma once

#include <windows.h>
#include <memory>
#include <thread>
#include "FileScanner.h"
#include "DatabaseManager.h"
#include "FileMetadata.h"

class MainForm {
public:
    MainForm();
    ~MainForm();

    bool Create();
    int Run();

private:
    HWND hMainWindow;
    HWND hPathEdit;
    HWND hBrowseButton;
    HWND hScanButton;
    HWND hFindDuplicatesButton;
    HWND hFileTypeEdit;
    HWND hResultsList;
    HWND hStatusBar;
    HWND hProgressBar;
    HWND hIncludeSubdirCheckbox;

    std::unique_ptr<FileScanner> fileScanner;
    std::unique_ptr<DatabaseManager> dbManager;
    std::vector<FileMetadata> lastScanResults;

    // Stop Scan Feature Members
    volatile bool isScanRunning = false;      // Flag to control scan state
    std::thread* pScanThread = nullptr;       // Pointer to background scan thread
    int filesScannedBeforeStop = 0;            // Count of files processed before stop
    volatile bool shouldResume = false;        // Flag to indicate resume mode
    std::string lastScanPath;                  // Path of last scan for resume

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnWindowResize(int width, int height);

    void OnBrowseClick();
    void OnScanClick();
    void OnStopScanClick();  // Handler for stop scan button click
    void OnScanComplete();
    void OnFindDuplicatesClick();  // Handler to find and display duplicates
    void UpdateStatusBar(const std::string& message);
    void DisplayResults(const std::vector<FileMetadata>& files);
    void DisplayDuplicates(const std::vector<std::vector<FileMetadata>>& duplicateGroups);
    void CreateControls();
    void RefreshFileTypeList();
};
