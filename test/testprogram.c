#include <windows.h>
#include <stdio.h>

int main() {
    HKEY hKey;
    const char* subkey = "SOFTWARE\\TEST";
    const char* valueName = "TestValue";
    const char* data = "blablabla";
    DWORD dataSize = strlen(data) + 1;
    
    LONG result = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        subkey,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        NULL,
        &hKey,
        NULL
    );
    
    if (result != ERROR_SUCCESS) {
        printf("FAILURE\n");
        return 1;
    }
    
    // Устанавливаем значение
    result = RegSetValueExA(
        hKey,
        valueName,
        0,
        REG_SZ,
        (const BYTE*)data,
        dataSize
    );
    
    if (result == ERROR_SUCCESS) {
        printf("SUCCESS\n");
    } else {
        printf("FAILURE\n");
    }
    
    // Закрываем ключ
    RegCloseKey(hKey);
    
    return 0;
}
