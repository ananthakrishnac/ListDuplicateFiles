#include "..\include\MainForm.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    MainForm form;

    if (!form.Create()) {
        MessageBox(nullptr, L"Failed to create main window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return form.Run();
}
