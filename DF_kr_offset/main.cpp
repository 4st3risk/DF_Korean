# include <iostream>
# include <vector>
# include <Windows.h>
# include <TlHelp32.h>
# include <string>
# include <fstream>

const std::wstring PROCESS_NAME = L"Dwarf Fortress.exe";

class MemoryScanner {
public:
	static DWORD GetProcessId(const std::wstring& processName) {
		DWORD pid = 0;
		HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapShot != INVALID_HANDLE_VALUE) {
			PROCESSENTRY32W pe32;
			pe32.dwSize = sizeof(PROCESSENTRY32W);
			if (Process32FirstW(hSnapShot, &pe32)) {
				do {
					if (processName == pe32.szExeFile) {
						pid = pe32.th32ProcessID;
						break;
					}
				} while (Process32NextW(hSnapShot, &pe32));
			}
			CloseHandle(hSnapShot);
		}
		return pid;
	}

	static uintptr_t GetModuleBaseAddress(DWORD pid, const std::wstring& moduleName) {
		uintptr_t modBaseAddr = 0;
		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (hSnapshot != INVALID_HANDLE_VALUE) {
			MODULEENTRY32W modEntry;
			modEntry.dwSize = sizeof(modEntry);
			if (Module32FirstW(hSnapshot, &modEntry)) {
				do {
					if (moduleName == modEntry.szModule) {
						modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
						break;
					}
				} while (Module32NextW(hSnapshot, &modEntry));
			}
			CloseHandle(hSnapshot);
		}
		return modBaseAddr;
	}
	static uintptr_t FindPattern(HANDLE hProcess, uintptr_t start, size_t size, size_t patternSize, const char* pattern, const char* mask) {
		std::vector<BYTE> memory(size);
		SIZE_T bytesRead;

		if (!ReadProcessMemory(hProcess, (LPCVOID)start, memory.data(), size, &bytesRead)) {
			printf("[!] ReadProcessMemory Failed with Error : %d \n",
			GetLastError());
			return 0;
		}

		size_t patternLength = patternSize;

		for (size_t i = 0; i < bytesRead - patternLength; i++) {
			bool found = true;
			for (size_t j = 0; j < patternLength; j++) {
				if (mask[j] == 'x' && pattern[j] != (char)memory[i + j]) {
					found = false;
					break;
				}
			}
			if (found) {
				return start + i;
			}

		}
		return 0;
	}
};

void SaveOffsetToFile(const std::string& filename, uintptr_t offset1, uintptr_t offset2) {
	std::ofstream outFile(filename, std::ios::out | std::ios::trunc);

	if (outFile.is_open()) {
		outFile << "DF_PTRST_OFFSET=0x" << std::hex << std::uppercase << offset1 << std::endl;
		outFile << "DF_ADDST_OFFSET=0x" << std::hex << std::uppercase << offset2 << std::endl;
		std::cout << "[SUCCESS] OFfset Saved in'" << filename << "' file." << std::endl;
		outFile.close();
	}
	else {
		std::cout << "[ERROR] Can't Create File." << std::endl;
	}
}


int main() {
	DWORD pid = MemoryScanner::GetProcessId(PROCESS_NAME);
	if (pid == 0) {
		std::cout << "Can't found Process. Make sure Game is running.\n" << std::endl;
		return -1;
	}

	std::cout << "Process ID Found: " << pid << std::endl;
	
	HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (!hProcess) {
		std::cout << "Can't Open Process. Try running administrator.\n" << std::endl;
		return -1;
	}

	uintptr_t moduleBase = MemoryScanner::GetModuleBaseAddress(pid, PROCESS_NAME);
	std::cout << "ModuleBase Address: 0x" << std::hex << moduleBase << std::endl;

	const char* pattern_ptrst =
		"\x48\x89\x5C\x24\x08"
		"\x48\x89\x6C\x24\x10"
		"\x48\x89\x74\x24\x18"
		"\x57"
		"\x41\x56"
		"\x41\x57"
		"\x48\x83\xEC\x20"
		"\x4D\x8B\xF0"
		"\x48\x8B\xEA"
		"\x48\x8B\xF1";
	const char* pattern_addst =
		"\x40\x53"
		"\x56"
		"\x57"
		"\x48\x83\xEC\x50"
		"\x48\x8B\x05\x21\x5C\xD9\x00"
		"\x48\x33\xC4"
		"\x48\x89\x44\x24\x40"
		"\x49\x63\xD9"
		"\x48\x8B\xF9";

	const char* mask_ptrst = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
	const char* mask_addst = "xxxxxxxxxxx????xxxxxxxxxxxxxx";

	size_t scanSize = 0x1000000;

	std::cout << "[#] Find Code Ptrst: ";
	for (size_t i = 0; i < strlen(pattern_ptrst); i++) {
		if (i % 6 == 0) {
			printf("\n");
		}
		printf("0x%02X ", pattern_ptrst[i] & 0xff);
		
	}
	std::cout << std::endl;

	std::cout << "[#] Find Code Addst: ";

	for (size_t i = 0; i < 30; i++) {
		if (i % 6 == 0) {
			printf("\n");
		}
		printf("0x%02X ", pattern_addst[i] & 0xff);

	}
	std::cout << std::endl;

	uintptr_t foundAddress_ptrst = MemoryScanner::FindPattern(hProcess, moduleBase, scanSize, 34, pattern_ptrst, mask_ptrst);
	uintptr_t foundAddress_addst = MemoryScanner::FindPattern(hProcess, moduleBase, scanSize, 30, pattern_addst, mask_addst);

	if (foundAddress_ptrst != 0 && foundAddress_addst != 0) {
		uintptr_t rva_ptrst = foundAddress_ptrst - moduleBase;
		uintptr_t rva_addst = foundAddress_addst - moduleBase;
		std::cout << "====================================================" << std::endl;
		std::cout << "Ptrst Func Found!" << std::endl;
		std::cout << "Absolute Address: 0x" << std::hex << foundAddress_ptrst << std::endl;
		std::cout << "Offset (RVA):		0x" << std::hex << rva_ptrst << std::endl;
		std::cout << "====================================================" << std::endl;

		std::cout << "====================================================" << std::endl;
		std::cout << "Addst Func Found!" << std::endl;
		std::cout << "Absolute Address: 0x" << std::hex << foundAddress_addst << std::endl;
		std::cout << "Offset (RVA):		0x" << std::hex << rva_addst << std::endl;
		std::cout << "====================================================" << std::endl;

		SaveOffsetToFile("offsets.txt", rva_ptrst, rva_addst);
	}
	else {
		std::cout << "Can't Found Pattern." << std::endl;
	}

	CloseHandle(hProcess);
	system("pause");
	return 0;
}

