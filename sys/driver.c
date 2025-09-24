
#include "regfltr.h"

typedef struct _DB_ELEMENT {
    char ObjectName[256];
    int Level;
} DB_ELEMENT, *PDB_ELEMENT;


LARGE_INTEGER g_RegistryCallbackCookie = { 0 };
BOOLEAN g_IsCallbackRegistered = FALSE;
BOOLEAN g_IsImageNotifyRegistered = FALSE;

HANDLE g_hLogFile = NULL;
ERESOURCE g_LogFileLock;
BOOLEAN g_LogFileInitialized = FALSE;

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DeviceUnload;

_Dispatch_type_(IRP_MJ_CREATE)         DRIVER_DISPATCH DeviceCreate;
_Dispatch_type_(IRP_MJ_CLOSE)          DRIVER_DISPATCH DeviceClose;
_Dispatch_type_(IRP_MJ_CLEANUP)        DRIVER_DISPATCH DeviceCleanup;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH DeviceControl;

//
// Pointer to the device object used to register registry callbacks
//
PDEVICE_OBJECT g_DeviceObj;

//
// Registry callback version
//
ULONG g_MajorVersion;
ULONG g_MinorVersion;

//
// Set to TRUE if TM and RM were successfully created and the transaction
// callback was successfully enabled. 
//
BOOLEAN g_RMCreated;


//
// OS version globals initialized in driver entry 
//

BOOLEAN g_IsWin8OrGreater = FALSE;

VOID
DetectOSVersion()
{

    RTL_OSVERSIONINFOEXW VersionInfo = {0};
    NTSTATUS Status;
    ULONGLONG ConditionMask = 0;

    //
    // Set VersionInfo to Win7's version number and then use
    // RtlVerifVersionInfo to see if this is win8 or greater.
    //
    
    VersionInfo.dwOSVersionInfoSize = sizeof(VersionInfo);
    VersionInfo.dwMajorVersion = 6;
    VersionInfo.dwMinorVersion = 1;

    VER_SET_CONDITION(ConditionMask, VER_MAJORVERSION, VER_LESS_EQUAL);
    VER_SET_CONDITION(ConditionMask, VER_MINORVERSION, VER_LESS_EQUAL);



    Status = RtlVerifyVersionInfo(&VersionInfo,
                                  VER_MAJORVERSION | VER_MINORVERSION,
                                  ConditionMask);
    if (NT_SUCCESS(Status)) {
        g_IsWin8OrGreater = FALSE;
        InfoPrint("DetectOSVersion: This machine is running Windows 7 or an older OS.");
    } else if (Status == STATUS_REVISION_MISMATCH) {
        g_IsWin8OrGreater = TRUE;
        InfoPrint("DetectOSVersion: This machine is running Windows 8 or a newer OS.");
    } else {
        ErrorPrint("RtlVerifyVersionInfo returned unexpected error status 0x%x.",
            Status);

        //
        // default action is to assume this is not win8
        //
        g_IsWin8OrGreater = FALSE;  
    }
    
}

NTSTATUS InitializeLogFile(VOID)
{
    NTSTATUS Status;
    HANDLE hFile = NULL;
    UNICODE_STRING FileName;
    OBJECT_ATTRIBUTES ObjAttr;
    IO_STATUS_BLOCK IoStatus;
    WCHAR LogPath[] = L"\\??\\C:\\regfltr_log.txt"; // Путь к файлу

    // Создаём папку logs, если не существует — но в ядре это сложно, поэтому лучше создать вручную
    RtlInitUnicodeString(&FileName, LogPath);

    InitializeObjectAttributes(&ObjAttr, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwCreateFile(
        &hFile,
        FILE_APPEND_DATA | SYNCHRONIZE,
        &ObjAttr,
        &IoStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OPEN_IF, // Создаёт, если не существует
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
        NULL,
        0
    );

    if (!NT_SUCCESS(Status)) {
        InfoPrint("Failed to create log file. Status: 0x%x", Status);
        return Status;
    }

    g_hLogFile = hFile;
    ExInitializeResourceLite(&g_LogFileLock);
    g_LogFileInitialized = TRUE;

    InfoPrint("Log file initialized: %wZ", &FileName);
    return STATUS_SUCCESS;
}

VOID WriteLogToFile(_In_ PCSTR LogMessage)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    ULONG BytesToWrite = (ULONG)strlen(LogMessage);
    CHAR StackBuffer[512]; // Используем стек для небольших сообщений

    if (!g_LogFileInitialized || g_hLogFile == NULL) {
        return;
    }

    if (BytesToWrite == 0) {
        return;
    }

    // Проверяем, помещается ли сообщение в стековый буфер
    if (BytesToWrite + 2 > sizeof(StackBuffer)) {
        BytesToWrite = sizeof(StackBuffer) - 3;
    }

    // Копируем в стековый буфер
    RtlCopyMemory(StackBuffer, LogMessage, BytesToWrite);
    StackBuffer[BytesToWrite] = '\r';
    StackBuffer[BytesToWrite + 1] = '\n';

    // Блокируем доступ к файлу
    ExAcquireResourceExclusiveLite(&g_LogFileLock, TRUE);

    Status = ZwWriteFile(
        g_hLogFile,
        NULL,
        NULL,
        NULL,
        &IoStatus,
        StackBuffer,
        BytesToWrite + 2,
        NULL,
        NULL
    );

    ExReleaseResourceLite(&g_LogFileLock);

    if (!NT_SUCCESS(Status)) {
        InfoPrint("Callback: WriteLogToFile failed: 0x%x", Status);
    }
}

NTSTATUS read_registry_value(
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PUNICODE_STRING ValueName,
    _Out_ PVOID* Buffer,
    _Out_ PSIZE_T DataSize
) {
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE hKey = NULL;
    OBJECT_ATTRIBUTES objAttr;
    KEY_VALUE_PARTIAL_INFORMATION* keyValueInfo = NULL;
    ULONG resultLength = 0;

    // Инициализируем атрибуты объекта
    InitializeObjectAttributes(&objAttr,
                             RegistryPath,
                             OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                             NULL,
                             NULL);

    // Открываем ключ реестра
    status = ZwOpenKey(&hKey, KEY_READ, &objAttr);
    if (!NT_SUCCESS(status)) {
        InfoPrint("Callback: Cannot open key: 0x%X\n", status);
        return status;
    }

    // Получаем размер данных
    status = ZwQueryValueKey(hKey,
                           ValueName,
                           KeyValuePartialInformation,
                           NULL,
                           0,
                           &resultLength);

    if (status != STATUS_BUFFER_TOO_SMALL) {
        InfoPrint("Callback: Cannot get value size: 0x%X\n", status);
        ZwClose(hKey);
        return status;
    }

    // Выделяем память в пуле ядра
    keyValueInfo = (KEY_VALUE_PARTIAL_INFORMATION*)
        ExAllocatePool2(POOL_FLAG_NON_PAGED, resultLength, 'RegD');

    if (!keyValueInfo) {
        InfoPrint("Callback: Memory allocation failed\n");
        ZwClose(hKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Читаем данные из реестра
    status = ZwQueryValueKey(hKey,
                           ValueName,
                           KeyValuePartialInformation,
                           keyValueInfo,
                           resultLength,
                           &resultLength);

    if (!NT_SUCCESS(status)) {
        InfoPrint("Callback: Cannot read value: 0x%X\n", status);
        ExFreePoolWithTag(keyValueInfo, 'RegD');
        ZwClose(hKey);
        return status;
    }

    // Проверяем тип данных
    if (keyValueInfo->Type != REG_SZ && keyValueInfo->Type != REG_EXPAND_SZ) {
        InfoPrint("Callback: Invalid registry value type: %lu\n", keyValueInfo->Type);
        ExFreePoolWithTag(keyValueInfo, 'RegD');
        ZwClose(hKey);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    // Выделяем память под буфер для возврата
    SIZE_T bufferSize = keyValueInfo->DataLength + sizeof(WCHAR);
    PVOID outputBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, bufferSize, 'BufD');
    
    if (!outputBuffer) {
        InfoPrint("Callback: Output buffer allocation failed\n");
        ExFreePoolWithTag(keyValueInfo, 'RegD');
        ZwClose(hKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Копируем данные
    RtlCopyMemory(outputBuffer, keyValueInfo->Data, keyValueInfo->DataLength);

    // Добавляем нулевой терминатор
    PWCHAR stringBuffer = (PWCHAR)outputBuffer;
    stringBuffer[keyValueInfo->DataLength / sizeof(WCHAR)] = L'\0';

    // Возвращаем результаты
    *Buffer = outputBuffer;
    *DataSize = keyValueInfo->DataLength;

    // Освобождаем ресурсы
    ExFreePoolWithTag(keyValueInfo, 'RegD');
    ZwClose(hKey);

    return STATUS_SUCCESS;
}


// Простая реализация isdigit для драйвера
int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

// Простая реализация atoi для драйвера
int atoi(const char* str) {
    int result = 0;
    int sign = 1;

    // Пропускаем пробелы
    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    // Проверяем знак
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    // Преобразуем цифры в число
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

PDB_ELEMENT parse_json_to_db_elements(
    _In_ PCHAR json_str,
    _Out_ PULONG count
) {
    if (!json_str || !count) {
        InfoPrint("Callback: [JSON] Error: json_str or count is NULL\n");
        return NULL;
    }

    *count = 0;
    PCHAR ptr = json_str;
    ULONG element_count = 0;

    // Подсчёт количества кавычек → кол-во строк = кавычки / 2
    while (*ptr) {
        if (*ptr == '"') {
            element_count++;
        }
        ptr++;
    }

    element_count /= 2;

    if (element_count == 0) {
        InfoPrint("Callback: [JSON] No key-value pairs found\n");
        return NULL;
    }

    // Выделение памяти
    SIZE_T alloc_size = element_count * sizeof(DB_ELEMENT);
    PDB_ELEMENT elements = (PDB_ELEMENT)ExAllocatePool2(POOL_FLAG_NON_PAGED, alloc_size, 'Json');
    if (!elements) {
        InfoPrint("Callback: [JSON] Failed to allocate %zu bytes for %lu elements\n", 
                  alloc_size, element_count);
        return NULL;
    }

    RtlZeroMemory(elements, alloc_size);

    // Парсим JSON
    ptr = json_str;
    ULONG index = 0;
    BOOLEAN in_string = FALSE;
    CHAR current_key[256] = {0};
    ULONG key_index = 0;

    while (*ptr && index < element_count) {
        if (*ptr == '"' && !in_string) {
            in_string = TRUE;
            key_index = 0;
            RtlZeroMemory(current_key, sizeof(current_key));
        }
        else if (*ptr == '"' && in_string) {
            in_string = FALSE;
            current_key[key_index] = '\0'; // Завершаем строку

            NTSTATUS copyStatus = RtlStringCbCopyNA(
                elements[index].ObjectName,
                sizeof(elements[index].ObjectName),
                current_key,
                key_index
            );

            if (!NT_SUCCESS(copyStatus)) {
                InfoPrint("Callback: [JSON] Failed to copy key '%s' (status: 0x%X)\n", 
                          current_key, copyStatus);
                ExFreePoolWithTag(elements, 'Json');
                return NULL;
            }
        }
        else if (in_string) {
            if (key_index < sizeof(current_key) - 1) {
                current_key[key_index++] = *ptr;
            } else {
                InfoPrint("Callback: [JSON] Key too long, truncating\n");
            }
        }
        else if ((*ptr >= '0' && *ptr <= '9') && key_index > 0) {
            // Парсим число вручную (без atoi и strlen!)
            ULONG value = 0;

            while (*ptr >= '0' && *ptr <= '9') {
                value = value * 10 + (*ptr - '0');
                ptr++;
            }

            elements[index].Level = value;

            index++;
            key_index = 0; // сбрасываем ключ
            RtlZeroMemory(current_key, sizeof(current_key));

            // Важно: ptr уже указывает за последнюю цифру,
            // но цикл ниже сделает ptr++, поэтому откатываем на 1 назад
            ptr--; 
        }

        ptr++;
    }

    *count = index;

    if (index == 0) {
        InfoPrint("Callback: [JSON] Warning: No valid key-value pairs were parsed!\n");
        ExFreePoolWithTag(elements, 'Json');
        return NULL;
    }

    return elements;
}


NTSTATUS test_read_db() {
    NTSTATUS status;
    PVOID buffer = NULL;
    SIZE_T dataSize = 0;

    UNICODE_STRING registryPath;
    UNICODE_STRING valueName;
    WCHAR registryPathStr[] = L"\\Registry\\Machine\\SOFTWARE\\Regfltr";
    WCHAR valueNameStr[] = L"Database";

    // Инициализируем UNICODE_STRING для пути реестра
    RtlInitUnicodeString(&registryPath, registryPathStr);
    RtlInitUnicodeString(&valueName, valueNameStr);

    // Читаем значение из реестра
    status = read_registry_value(&registryPath, &valueName, &buffer, &dataSize);

    if (NT_SUCCESS(status) && buffer) {
        InfoPrint("Callback: Registry value size: %zu bytes\n", dataSize);

        // Данные в буфере - это Unicode строка (WCHAR)
        PWCHAR unicodeString = (PWCHAR)buffer;
        
        // Если нужно преобразовать в ANSI для парсинга
        ANSI_STRING ansiString;
        UNICODE_STRING unicodeStr;

        // Создаем UNICODE_STRING из буфера
        unicodeStr.Buffer = unicodeString;
        unicodeStr.Length = (USHORT)dataSize;
        unicodeStr.MaximumLength = (USHORT)dataSize + sizeof(WCHAR);

        // Преобразуем в ANSI
        status = RtlUnicodeStringToAnsiString(&ansiString, &unicodeStr, TRUE);
        if (NT_SUCCESS(status)) {
            InfoPrint("Callback: JSON as ANSI: %s\n", ansiString.Buffer);

            ULONG count = 0;
            PDB_ELEMENT elements = parse_json_to_db_elements(
                ansiString.Buffer,
                &count
            );

            if (elements) {
                InfoPrint("Callback: Parsed %lu database elements:\n", count);
                for (ULONG i = 0; i < count; i++) {
                    InfoPrint("Callback: [%lu] ObjectName: '%s', Level: %lu\n",
                              i,
                              elements[i].ObjectName,
                              elements[i].Level);
                }

                // Освобождаем элементы
                ExFreePoolWithTag(elements, 'Json');
            } else {
                InfoPrint("Callback: Failed to parse JSON\n");
            }

            // Освобождаем ANSI строку
            RtlFreeAnsiString(&ansiString);
        } else {
            InfoPrint("Callback: Failed to convert to ANSI: 0x%X\n", status);
        }

        // Освобождаем память
        ExFreePoolWithTag(buffer, 'BufD');
    } else {
        InfoPrint("Callback: Failed to read registry value: 0x%X\n", status);
    }

    return status;
}


NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS Status;
    UNICODE_STRING NtDeviceName;
    UNICODE_STRING DosDevicesLinkName;
    UNICODE_STRING DeviceSDDLString;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, 
               DPFLTR_ERROR_LEVEL,
               "RegFltr: DriverEntry()\n");

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, 
               DPFLTR_ERROR_LEVEL,
               "RegFltr: Use ed nt!Kd_IHVDRIVER_Mask 8 to enable more detailed printouts\n");

    //
    //  Default to NonPagedPoolNx for non paged pool allocations where supported.
    //

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    //
    // Create our device object.
    //

    RtlInitUnicodeString(&NtDeviceName, NT_DEVICE_NAME);
    RtlInitUnicodeString(&DeviceSDDLString, DEVICE_SDDL);

    Status = IoCreateDeviceSecure(
                            DriverObject,                 // pointer to driver object
                            0,                            // device extension size
                            &NtDeviceName,                // device name
                            FILE_DEVICE_UNKNOWN,          // device type
                            0,                            // device characteristics
                            TRUE,                         // not exclusive
                            &DeviceSDDLString,            // SDDL string specifying access
                            NULL,                         // device class guid
                            &g_DeviceObj);                // returned device object pointer

    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    //
    // Set dispatch routines.
    //

    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DeviceCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DeviceClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = DeviceCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    DriverObject->DriverUnload                         = DeviceUnload;

    g_IsCallbackRegistered = FALSE;
    g_RegistryCallbackCookie.QuadPart = 0;

    g_IsImageNotifyRegistered = FALSE;

    PreNotificationLogSample();
    InitializeLogFile();
    test_read_db();

    //
    // Create a link in the Win32 namespace.
    //
    
    RtlInitUnicodeString(&DosDevicesLinkName, DOS_DEVICES_LINK_NAME);

    Status = IoCreateSymbolicLink(&DosDevicesLinkName, &NtDeviceName);

    if (!NT_SUCCESS(Status)) {
        IoDeleteDevice(DriverObject->DeviceObject);
        return Status;
    }

    //
    // Get callback version.
    //

    CmGetCallbackVersion(&g_MajorVersion, &g_MinorVersion);
    InfoPrint("Callback version %u.%u", g_MajorVersion, g_MinorVersion);

    //
    // Some variations depend on knowing if the OS is win8 or above
    //
    
    DetectOSVersion();

    //
    // Initialize the callback context list
    //

    InitializeListHead(&g_CallbackCtxListHead);
    ExInitializeFastMutex(&g_CallbackCtxListLock);
    g_NumCallbackCtxListEntries = 0;

    return STATUS_SUCCESS;
    
}



NTSTATUS
DeviceCreate (
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceClose (
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceCleanup (
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceControl (
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PIO_STACK_LOCATION IrpStack;
    ULONG Ioctl;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = STATUS_SUCCESS;

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    Ioctl = IrpStack->Parameters.DeviceIoControl.IoControlCode;

    // ioctl route
    switch (Ioctl)
    {

    case IOCTL_DO_KERNELMODE_SAMPLES:
        LoadImageNotifySample();
        break;

    case IOCTL_GET_CALLBACK_VERSION:
        Status = GetCallbackVersion(DeviceObject, Irp);
        break;

    default:
        ErrorPrint("Unrecognized ioctl code 0x%x", Ioctl);
    }

    //
    // Complete the irp and return.
    //

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;

}


VOID
DeviceUnload (
    _In_ PDRIVER_OBJECT DriverObject
    )
{


    if (g_IsCallbackRegistered) {
        CmUnRegisterCallback(g_RegistryCallbackCookie);
        InfoPrint("Unregistered callback with cookie: 0x%llx",
            g_RegistryCallbackCookie.QuadPart);
        g_IsCallbackRegistered = FALSE;
    }

    if (g_IsImageNotifyRegistered) {
        PsRemoveLoadImageNotifyRoutine(LoadImageNotifyRoutine);
        InfoPrint("Unregistered load image callback");
        g_IsImageNotifyRegistered = FALSE;
    }

    // Закрываем лог-файл
    if (g_LogFileInitialized && g_hLogFile != NULL) {
        ZwClose(g_hLogFile);
        g_hLogFile = NULL;
        ExDeleteResourceLite(&g_LogFileLock);
        g_LogFileInitialized = FALSE;
        InfoPrint("Log file closed callback");
    }

    UNICODE_STRING  DosDevicesLinkName;
    
    //
    // Delete the link from our device name to a name in the Win32 namespace.
    //

    RtlInitUnicodeString(&DosDevicesLinkName, DOS_DEVICES_LINK_NAME);
    IoDeleteSymbolicLink(&DosDevicesLinkName);

    //
    // Finally delete our device object
    //

    IoDeleteDevice(DriverObject->DeviceObject);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, 
               DPFLTR_ERROR_LEVEL,
               "RegFltr: DeviceUnload\n");
}

