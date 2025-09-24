#include "regctrl.h"

//
// Global variables
//

//
// Handle to the driver
//
HANDLE g_Driver;

//
// Handle to the root test key
//
HKEY g_RootKey;

//
// Version number for the registry callback
//
ULONG g_MajorVersion;
ULONG g_MinorVersion;


BOOL
GetCallbackVersion();

BOOL
ReadDatabase();

VOID
DoKernelModeSamples();

LPCWSTR 
GetKernelModeSampleName (
    _In_ KERNELMODE_SAMPLE Sample
    );


VOID __cdecl
wmain(
    _In_ ULONG argc,
    _In_reads_(argc) LPCWSTR argv[]
)
{
    BOOL Result;
    WCHAR Command[100];

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // Загружает драйвер (создает службу и тд)
    Result = UtilLoadDriver(DRIVER_NAME,
        DRIVER_NAME_WITH_EXT,
        WIN32_DEVICE_NAME,
        &g_Driver);

    if (Result != TRUE) {
        ErrorPrint("UtilLoadDriver failed, exiting...");
        exit(1);
    }

    printf("\n");
    printf("=== Registry Filter Driver Controller ===\n");
    printf("Driver loaded successfully!\n");

    // Получаем версию callback'ов
    if (GetCallbackVersion()) {
        InfoPrint("Callback version is %u.%u", g_MajorVersion, g_MinorVersion);
    }

    // Основной цикл программы
    while (TRUE) {
        printf("\n");
        printf("Available commands:\n");
        printf("1. exit    - Unload driver and exit\n");
        printf("2. toggle  - Toggle notify\n");
        printf("3. update  - Update database\n");
        printf("Enter command: ");

        // Читаем команду от пользователя
        if (fgetws(Command, ARRAYSIZE(Command), stdin) == NULL) {
            break;
        }

        // Убираем символ новой строки
        Command[wcslen(Command) - 1] = L'\0';

        // Обрабатываем команду
        if (_wcsicmp(Command, L"exit") == 0 || _wcsicmp(Command, L"1") == 0) {
            printf("Exiting...\n");
            break;
        }
        else if (_wcsicmp(Command, L"toggle") == 0 || _wcsicmp(Command, L"2") == 0) {
            printf("Sending...\n");
            DoKernelModeSamples();
            continue;
        }
        else if (_wcsicmp(Command, L"update") == 0 || _wcsicmp(Command, L"3") == 0) {
            printf("Sending...\n");
            ReadDatabase();
            continue;
        }
        else if (wcslen(Command) == 0) {
            // Пустая строка - продолжаем цикл
            continue;
        }
        else {
            printf("Unknown command: %ws\n", Command);
        }
    }

    // Выгружает драйвер - удаляет службу и тд
    printf("Unloading driver...\n");
    UtilUnloadDriver(g_Driver, NULL, DRIVER_NAME);
    printf("Driver unloaded. Goodbye!\n");
}


BOOL
GetCallbackVersion(
    ) 
{

    DWORD BytesReturned = 0;
    BOOL Result;
    GET_CALLBACK_VERSION_OUTPUT Output = {0};
    
    Result = DeviceIoControl(g_Driver,
                              IOCTL_GET_CALLBACK_VERSION,
                              NULL,
                              0,
                              &Output,
                              sizeof(GET_CALLBACK_VERSION_OUTPUT),
                              &BytesReturned,
                              NULL);

    if (Result != TRUE) {
        ErrorPrint("DeviceIoControl for GET_CALLBACK_VERSION failed, error %d\n", GetLastError());
        return FALSE;
    }

    g_MajorVersion = Output.MajorVersion;
    g_MinorVersion = Output.MinorVersion;

    return TRUE;

}

BOOL
ReadDatabase(
    )
{

    DWORD BytesReturned = 0;
    BOOL Result;
    
    Result = DeviceIoControl(g_Driver,
                             IOCTL_READ_DB,
                             NULL,
                             0,
                             NULL,
                             0,
                             &BytesReturned,
                             NULL);

    if (Result != TRUE) {
        ErrorPrint("DeviceIoControl for IOCTL_READ_DB failed, error %d\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}

VOID
DoKernelModeSamples(
    ) 
{

    UINT Index;
    DWORD BytesReturned = 0;
    BOOL Result;
    DO_KERNELMODE_SAMPLES_OUTPUT Output = {0};
    
    Result = DeviceIoControl (g_Driver,
                              IOCTL_DO_KERNELMODE_SAMPLES,
                              NULL,
                              0,
                              &Output,
                              sizeof(DO_KERNELMODE_SAMPLES_OUTPUT),
                              &BytesReturned,
                              NULL);

    if (Result != TRUE) {
        ErrorPrint("DeviceIoControl for DO_KERNELMODE_SAMPLES failed, error %d\n", GetLastError());
        return;
    }

    InfoPrint("");
    InfoPrint("=== Results of KernelMode Samples ===");

    for (Index = 0; Index < MAX_KERNELMODE_SAMPLES; Index++) {
        InfoPrint("\t%S: %s",
                  GetKernelModeSampleName(Index),
                  Output.SampleResults[Index]? "Succeeded" : "FAILED");
    }

}

LPCWSTR 
GetKernelModeSampleName (
    _In_ KERNELMODE_SAMPLE Sample
    )
/*++

Routine Description:

    Converts from a KERNELMODE_SAMPLE value to a string

Arguments:

    Sample - value that identifies a kernel mode sample

Return Value:

    Returns a string of the name of Sample.
    
--*/
{
    switch (Sample) {
        case KERNELMODE_SAMPLE_PRE_NOTIFICATION_LOG:
            return L"Pre-Notification Log Sample";
        default:
            return L"Unsupported Kernel Mode Sample";
    }
}

