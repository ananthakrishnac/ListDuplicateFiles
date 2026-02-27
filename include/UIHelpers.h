#pragma once

#include <windows.h>
#include <string>

// Color definitions for modern UI
namespace UIColors {
    // Modern color palette
    const COLORREF BACKGROUND = RGB(250, 250, 252);       // Almost white, subtle blue tint
    const COLORREF HEADER_BG = RGB(240, 245, 250);        // Light blue header
    const COLORREF PANEL_BG = RGB(255, 255, 255);         // Pure white panels
    const COLORREF SHADOW = RGB(230, 230, 235);           // Subtle shadow

    // Primary & Secondary colors
    const COLORREF PRIMARY = RGB(25, 118, 210);           // Deep blue
    const COLORREF PRIMARY_HOVER = RGB(30, 136, 229);     // Brighter blue hover
    const COLORREF PRIMARY_LIGHT = RGB(66, 165, 245);     // Light blue
    const COLORREF SECONDARY = RGB(102, 102, 102);        // Medium gray
    const COLORREF SECONDARY_LIGHT = RGB(158, 158, 158);  // Light gray

    // Text colors
    const COLORREF TEXT_PRIMARY = RGB(33, 33, 33);        // Dark text
    const COLORREF TEXT_SECONDARY = RGB(102, 102, 102);   // Medium gray text
    const COLORREF TEXT_HINT = RGB(158, 158, 158);        // Light hint text

    // Status colors
    const COLORREF SUCCESS = RGB(56, 142, 60);            // Green
    const COLORREF WARNING = RGB(251, 140, 0);            // Orange
    const COLORREF STATUS_ERROR = RGB(211, 47, 47);       // Red
    const COLORREF INFO = RGB(25, 118, 210);              // Blue

    // Control colors
    const COLORREF INPUT_BG = RGB(255, 255, 255);         // White input
    const COLORREF INPUT_BORDER = RGB(189, 189, 189);     // Gray border
    const COLORREF INPUT_BORDER_FOCUS = RGB(25, 118, 210);// Blue border on focus
    const COLORREF BUTTON_BG = RGB(25, 118, 210);         // Button blue
    const COLORREF BUTTON_TEXT = RGB(255, 255, 255);      // White text on button
}

// Font helpers
class FontHelper {
public:
    // Regular body text font (13px)
    static HFONT CreateDefaultFont() {
        return CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Bold text font (12-14px)
    static HFONT CreateBoldFont(int size = 14) {
        return CreateFont(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Large title font (18px bold)
    static HFONT CreateTitleFont() {
        return CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Header/Section font (16px bold)
    static HFONT CreateHeaderFont() {
        return CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Button/Label font (13px bold)
    static HFONT CreateButtonFont() {
        return CreateFont(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Small text font (11px)
    static HFONT CreateSmallFont() {
        return CreateFont(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    // Monospace font for status/debug (11px)
    static HFONT CreateMonospaceFont() {
        return CreateFont(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_MODERN, L"Consolas");
    }
};

// Layout helpers
class LayoutHelper {
public:
    static const int MARGIN = 20;
    static const int SPACING = 55;
    static const int CONTROL_HEIGHT = 36;
    static const int BUTTON_HEIGHT = 56;
    static const int SECTION_GAP = 15;

    static int GetWindowWidth() { return 1100; }
    static int GetWindowHeight() { return 800; }

    static int GetContentWidth() {
        return GetWindowWidth() - (MARGIN * 2);
    }
};

// Button styling helper
class ButtonStyler {
public:
    // Primary button (main action - filled with blue)
    static void SetPrimaryStyle(HWND hButton) {
        SendMessage(hButton, WM_SETFONT, (WPARAM)FontHelper::CreateButtonFont(), TRUE);
        // Set button colors via BS_OWNERDRAW would require custom message handling
        // For now, rely on system rendering with fonts
    }

    // Secondary button (alternate action - outlined)
    static void SetSecondaryStyle(HWND hButton) {
        SendMessage(hButton, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
    }

    // Small button (compact size)
    static void SetSmallStyle(HWND hButton) {
        SendMessage(hButton, WM_SETFONT, (WPARAM)FontHelper::CreateSmallFont(), TRUE);
    }

    static void SetIconButton(HWND hButton, HICON icon, bool isDefault = false) {
        if (isDefault) {
            SetPrimaryStyle(hButton);
        } else {
            SetSecondaryStyle(hButton);
        }

        if (icon) {
            SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);
        }
    }
};

// Label styling helper
class LabelStyler {
public:
    // Section header (16px bold, dark text)
    static void SetSectionTitle(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateHeaderFont(), TRUE);
        SetTextColor(hLabel, UIColors::TEXT_PRIMARY);
    }

    // Main title (18px bold)
    static void SetTitleFont(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateTitleFont(), TRUE);
        SetTextColor(hLabel, UIColors::TEXT_PRIMARY);
    }

    // Header font (16px bold)
    static void SetHeaderFont(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateHeaderFont(), TRUE);
        SetTextColor(hLabel, UIColors::TEXT_PRIMARY);
    }

    // Normal body text (13px regular)
    static void SetNormalLabel(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
        SetTextColor(hLabel, UIColors::TEXT_SECONDARY);
    }

    // Hint text (smaller, lighter)
    static void SetHintLabel(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateSmallFont(), TRUE);
        SetTextColor(hLabel, UIColors::TEXT_HINT);
    }

    // Success status (green)
    static void SetSuccessLabel(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateBoldFont(12), TRUE);
        SetTextColor(hLabel, UIColors::SUCCESS);
    }

    // Error status (red)
    static void SetErrorLabel(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateBoldFont(12), TRUE);
        SetTextColor(hLabel, UIColors::STATUS_ERROR);
    }

    // Warning status (orange)
    static void SetWarningLabel(HWND hLabel) {
        SendMessage(hLabel, WM_SETFONT, (WPARAM)FontHelper::CreateBoldFont(12), TRUE);
        SetTextColor(hLabel, UIColors::WARNING);
    }

private:
    static void SetTextColor(HWND hLabel, COLORREF color) {
        // Note: This sets the text color for WM_PAINT, not direct label color
        // Actual color rendering depends on system settings and label type
    }
};

// Input field styling helper
class InputStyler {
public:
    static void SetDefaultStyle(HWND hInput) {
        SendMessage(hInput, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
    }

    static void SetErrorStyle(HWND hInput) {
        SendMessage(hInput, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
        // Visual indication of error (could be red border in owner-draw)
    }

    static void SetSuccessStyle(HWND hInput) {
        SendMessage(hInput, WM_SETFONT, (WPARAM)FontHelper::CreateDefaultFont(), TRUE);
        // Visual indication of success (could be green border in owner-draw)
    }
};

// Window/Panel styling helper
class PanelStyler {
public:
    static void SetHeaderPanel(HWND hPanel) {
        // Set background color and border
        // Note: Requires custom WM_PAINT for actual color rendering
    }

    static void SetContentPanel(HWND hPanel) {
        // Set content area styling
    }
};

// Icon and image helpers
class IconStyler {
public:
    // Load icon from resource with scaling
    static HICON LoadScaledIcon(UINT iconId, int size) {
        // Returns scaled icon for modern displays
        return nullptr;  // Placeholder
    }

    // Create colored rect (for visual indicators)
    static HBRUSH CreateStatusBrush(COLORREF color) {
        return CreateSolidBrush(color);
    }
};
