#pragma once

#include <windows.h>
#include <string>

class IconLoader {
public:
    // Load icon from file and apply to button
    static void ApplyIconFromFile(HWND hButton, const wchar_t* iconPath) {
        HICON hIcon = (HICON)LoadImageW(nullptr, iconPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        if (hIcon) {
            SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
        }
    }

    // Load icon from file
    static HICON LoadIconFromFile(const wchar_t* iconPath) {
        return (HICON)LoadImageW(nullptr, iconPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    }
};
