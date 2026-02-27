#pragma once

#include <windows.h>

class IconManager {
public:
    // Get folder icon for window icon
    static HICON GetFolderIcon() {
        // Load folder icon - use application icon as fallback
        HICON hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        return hIcon;
    }

private:
    IconManager() {}
    ~IconManager() {}
};
