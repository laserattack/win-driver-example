
#include "regfltr.h"


FAST_MUTEX g_CallbackCtxListLock;
LIST_ENTRY g_CallbackCtxListHead;
USHORT g_NumCallbackCtxListEntries;

NTSTATUS read_db() {
    NTSTATUS status;
    PVOID buffer = NULL;
    SIZE_T dataSize = 0;

    UNICODE_STRING registryPath;
    UNICODE_STRING valueName;
    WCHAR registryPathStr[] = L"\\Registry\\Machine\\SOFTWARE\\Regfltr";
    WCHAR valueNameStr[] = L"Database";

    ExAcquireResourceExclusiveLite(&g_db_elementsLock, TRUE);

    // Если уже заполнена база данных, то надо сначала ее очистить
    if (g_db_elements != NULL) {
        InfoPrint("Callback: Freeing existing database elements\n");
        ExFreePoolWithTag(g_db_elements, 'Json');
        g_db_elements = NULL;
        g_db_elements_count = 0;
    }

    ExReleaseResourceLite(&g_db_elementsLock);

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

            ExAcquireResourceExclusiveLite(&g_db_elementsLock, TRUE);

            InfoPrint("Callback: JSON as ANSI: %s\n", ansiString.Buffer);

            ULONG count = 0;
            g_db_elements = parse_json_to_db_elements(
                ansiString.Buffer,
                &count
            );

            g_db_elements_count = count;

            if (g_db_elements) {
                InfoPrint("Callback: Parsed %lu database elements:\n", count);
                for (ULONG i = 0; i < count; i++) {
                    InfoPrint("Callback: [%lu] ObjectName: %s, Level: %lu\n",
                              i,
                              g_db_elements[i].ObjectName,
                              g_db_elements[i].Level);
                }
            } else {
                InfoPrint("Callback: Failed to parse JSON\n");
            }

            // Освобождаем ANSI строку
            RtlFreeAnsiString(&ansiString);

            // Освобождаем мьютекс
            ExReleaseResourceLite(&g_db_elementsLock);
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

// Функция для поиска уровня (Level) по имени объекта в CHAR строке
NTSTATUS get_level_by_object_name_char(
    _In_ PCHAR object_name,
    _Out_ PULONG level
)
{
    NTSTATUS status = STATUS_NOT_FOUND;

    // Проверяем валидность параметров
    if (object_name == NULL || level == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    // Проверяем инициализацию базы данных
    if (g_db_elements == NULL) {
        return STATUS_NOT_FOUND;
    }

    // Захватываем мьютекс для разделяемого доступа (чтение)
    ExAcquireResourceExclusiveLite(&g_db_elementsLock, TRUE);

    __try {
        // Ищем объект в базе данных
        for (ULONG i = 0; i < g_db_elements_count; i++) {
            // Сравниваем строки (без учета регистра)
            if (_stricmp(g_db_elements[i].ObjectName, object_name) == 0) {
                *level = g_db_elements[i].Level;
                status = STATUS_SUCCESS;

                // Логируем найденное значение для отладки
                InfoPrint("Found level for object %s: %lu\n", object_name, *level);
                break;
            }
        }

        if (!NT_SUCCESS(status)) {
            InfoPrint("Object %s not found in database\n", object_name);
        }
    }
    __finally {
        // Всегда освобождаем мьютекс
        ExReleaseResourceLite(&g_db_elementsLock);
    }

    return status;
}

// NOTE: Проверяет есть ли доступ у процесса к ключу
NTSTATUS access_check(
    _In_ PCHAR object_name
) {
    ULONG level = 0;
    NTSTATUS status;
    status = get_level_by_object_name_char(object_name, &level);

    return status;
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

PVOID
CreateCallbackContext(
    _In_ CALLBACK_MODE CallbackMode,
    _In_ PCWSTR AltitudeString
    ) 
{

    PCALLBACK_CONTEXT CallbackCtx = NULL;
    NTSTATUS Status;
    BOOLEAN Success = FALSE;

    CallbackCtx = (PCALLBACK_CONTEXT) ExAllocatePoolZero (
                        PagedPool,
                        sizeof(CALLBACK_CONTEXT),
                        REGFLTR_CONTEXT_POOL_TAG);

    if (CallbackCtx == NULL) {
        ErrorPrint("CreateCallbackContext failed due to insufficient resources.");
        goto Exit;
    }

    CallbackCtx->CallbackMode = CallbackMode;
    CallbackCtx->ProcessId = PsGetCurrentProcessId();

    Status = RtlStringCbPrintfW(CallbackCtx->AltitudeBuffer,
                                 MAX_ALTITUDE_BUFFER_LENGTH * sizeof(WCHAR),
                                 L"%s",
                                 AltitudeString);
    
    if (!NT_SUCCESS(Status)) {
        ErrorPrint("RtlStringCbPrintfW in CreateCallbackContext failed. Status 0x%x", Status);
        goto Exit;
    }
    
    RtlInitUnicodeString (&CallbackCtx->Altitude, CallbackCtx->AltitudeBuffer);

    Success = TRUE;

  Exit:

    if (Success == FALSE) {
        if (CallbackCtx != NULL) {
            ExFreePoolWithTag(CallbackCtx, REGFLTR_CONTEXT_POOL_TAG);
            CallbackCtx = NULL;
        }
    }

    return CallbackCtx;
    
}
