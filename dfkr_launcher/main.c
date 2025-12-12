#include <stdio.h>
#include <windows.h>

# define TARGET_EXE "Dwarf Fortress.exe"
# define MY_DLL_NAME "Dwarf_hook.dll"


int main()
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char current_dir[MAX_PATH];
    //GetCurrentDirectoryA(MAX_PATH, current_dir);

    char dll_path[MAX_PATH];
    sprintf_s(dll_path, MAX_PATH, "%s", MY_DLL_NAME);

    printf("=== Debug Launcher ===\n");
    printf("[1] Checking Files...\n");
    printf(" - Path: %s\n", dll_path);

    if (GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
        printf("[Error] DLL file NOT found! Check filename.\n");
        system("pause");
        return 1;
    }
    printf(" - DLL Found: OK\n");

    printf("[2] Starting Game (Suspended)...\n");
    if (!CreateProcessA(NULL, TARGET_EXE, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        printf("[Error] Failed to start '%s'. (Code: %d)\n", TARGET_EXE, GetLastError());
        printf(" -> Make sure launcher is in the game folder.\n");
        system("pause");
        return 1;
    }
    printf(" - Process ID: %d\n", pi.dwProcessId);

    printf("[3] Injecting DLL...\n");
    void* pLoadLibrary = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLoadLibrary) {
        printf("[Error] Failed to find LoadLibraryA.\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    void* pRemoteBuf = VirtualAllocEx(pi.hProcess, NULL, strlen(dll_path) + 1, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuf) {
        printf("[Error] Memory Allocation failed.\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    if (!WriteProcessMemory(pi.hProcess, pRemoteBuf, (void*)dll_path, strlen(dll_path) + 1, NULL)) {
        printf("[Error] WriteProcessMemory failed.\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, pRemoteBuf, 0, NULL);
    if (!hThread) {
        printf("[Error] CreateRemoteThread failed. (Anti-Virus blocking?)\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    if (exitCode == 0) {
        printf("\n[CRITICAL ERROR] Injection FAILED inside the game!\n");
        printf(" -> LoadLibrary returned NULL.\n");
        printf(" -> Possible Causes:\n");
        printf("    1. Missing Dependencies (Is MinHook.x64.dll in the folder?)\n");
        printf("    2. Architecture Mismatch (Did you compile Launcher/DLL as x64?)\n");

        CloseHandle(hThread);
        VirtualFreeEx(pi.hProcess, pRemoteBuf, 0, MEM_RELEASE);
        TerminateProcess(pi.hProcess, 1);
        system("pause");
        return 1;
    }

    printf(" - Injection Result: SUCCESS (Handle: 0x%X)\n", exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(pi.hProcess, pRemoteBuf, 0, MEM_RELEASE);

    printf("[4] Resuming Game...\n");
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("Done.\n");
    Sleep(2000);
    return 0;
}