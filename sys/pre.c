#include "regfltr.h"


BOOLEAN
PreNotificationLogSample(
)
{
    PCALLBACK_CONTEXT CallbackCtx = NULL;
    NTSTATUS Status;
    BOOLEAN Success = FALSE;

    InfoPrint("");
    InfoPrint("=== Pre-Notification Log Sample ====");

    //
    // Create the callback context
    //

    CallbackCtx = CreateCallbackContext(CALLBACK_MODE_PRE_NOTIFICATION_LOG,
        CALLBACK_ALTITUDE);
    if (CallbackCtx == NULL) {
        goto Exit;
    }

    //
    // Register callback
    //

    Status = CmRegisterCallbackEx(Callback,
        &CallbackCtx->Altitude,
        g_DeviceObj->DriverObject,
        (PVOID)CallbackCtx,
        &CallbackCtx->Cookie,
        NULL);
    if (!NT_SUCCESS(Status)) {
        ErrorPrint("CmRegisterCallback failed. Status 0x%x", Status);
        goto Exit;
    }

    g_RegistryCallbackCookie = CallbackCtx->Cookie;
    g_IsCallbackRegistered = TRUE;

    Success = TRUE;
    InfoPrint("Callback registered successfully. Starting to log registry operations...");

Exit:

    if (CallbackCtx != NULL) {
    }

    if (Success) {
        InfoPrint("Pre-Notification Log Sample succeeded.");
    }
    else {
        ErrorPrint("Pre-Notification Log Sample FAILED.");
    }

    return Success;
}

BOOLEAN
LoadImageNotifySample(
    VOID
)
{
    NTSTATUS Status;
    BOOLEAN Success = FALSE;

    InfoPrint("");
    InfoPrint("=== Load Image Notify Sample ===");

    //
    // Переключаем состояние: если зарегистрирован — убираем, если нет — ставим
    //

    if (g_IsImageNotifyRegistered) {
        // Уже зарегистрирован → отменяем
        InfoPrint("Load image notify is currently registered. Unregistering...");

        PsRemoveLoadImageNotifyRoutine(LoadImageNotifyRoutine);
        g_IsImageNotifyRegistered = FALSE;

        InfoPrint("Load image notify unregistered successfully.");
        Success = TRUE;
        goto Exit;
    }
    else {
        // Не зарегистрирован → регистрируем
        InfoPrint("Load image notify is not registered. Registering...");

        Status = PsSetLoadImageNotifyRoutine(LoadImageNotifyRoutine);
        if (!NT_SUCCESS(Status)) {
            ErrorPrint("PsSetLoadImageNotifyRoutine failed. Status 0x%x", Status);
            goto Exit;
        }

        g_IsImageNotifyRegistered = TRUE;
        Success = TRUE;

        InfoPrint("Load image notify registered successfully. Monitoring module loads...");
    }

Exit:
    if (Success) {
        if (g_IsImageNotifyRegistered) {
            InfoPrint("Load Image Notify Sample: ENABLED.");
        }
        else {
            InfoPrint("Load Image Notify Sample: DISABLED.");
        }
    }
    else {
        ErrorPrint("Load Image Notify Sample FAILED.");
    }

    return Success;
}

VOID
LoadImageNotifyRoutine(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
)
{
    UNREFERENCED_PARAMETER(ImageInfo);

    // --- Получение и форматирование времени ---
    LARGE_INTEGER SystemTime, LocalTime;
    TIME_FIELDS TimeFields;
    CHAR TimeBuffer[64] = { 0 };

    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &TimeFields);

    _snprintf_s(TimeBuffer, sizeof(TimeBuffer), _TRUNCATE,
        "%04d-%02d-%02d %02d:%02d:%02d.%06d",
        TimeFields.Year,
        TimeFields.Month,
        TimeFields.Day,
        TimeFields.Hour,
        TimeFields.Minute,
        TimeFields.Second,
        TimeFields.Milliseconds * 1000);

    // --- Получение имени процесса по PID ---
    CHAR ProcessName[16] = { 0 };
    PEPROCESS Process = NULL;

    if (NT_SUCCESS(PsLookupProcessByProcessId(ProcessId, &Process))) {
        PCHAR ImageName = (PCHAR)((ULONG_PTR)Process + 0x5a8);

        __try {
            if (ImageName != NULL) {
                strncpy(ProcessName, ImageName, 15);
                ProcessName[15] = '\0';
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            strcpy(ProcessName, "Unknown");
        }

        ObDereferenceObject(Process);
    }
    else {
        strcpy(ProcessName, "Unknown");
    }

    // --- Формируем строку лога ---
    CHAR FinalLogLine[512] = { 0 };

    if (FullImageName != NULL) {
        _snprintf_s(FinalLogLine, sizeof(FinalLogLine), _TRUNCATE,
            "[%s] [PID: %lld, Process: %s] Callback: Module: %wZ",
            TimeBuffer,
            (LONG64)ProcessId,
            ProcessName,
            FullImageName);
    } else {
        _snprintf_s(FinalLogLine, sizeof(FinalLogLine), _TRUNCATE,
            "[%s] [PID: %lld, Process: %s] Callback: Module: <unknown>",
            TimeBuffer,
            (LONG64)ProcessId,
            ProcessName);
    }

    // --- Выводим в отладку ---
    InfoPrint("%s", FinalLogLine);

    // --- Дублируем в файл ---
    WriteLogToFile(FinalLogLine);
}

// NOTE: Callback на операции с реестром
NTSTATUS
CallbackPreNotificationLog(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _Inout_ PVOID Argument2
    )
{

    NTSTATUS Status = STATUS_SUCCESS;
    PREG_CREATE_KEY_INFORMATION PreCreateInfo;
    PREG_SET_VALUE_KEY_INFORMATION PreSetValueInfo;

    UNREFERENCED_PARAMETER(CallbackCtx);

    HANDLE ProcessId = PsGetCurrentProcessId();
    PEPROCESS Process = PsGetCurrentProcess();
    CHAR ProcessName[16] = {0};

    // --- ДОБАВЛЕНО: Получение и форматирование времени ---
    LARGE_INTEGER SystemTime, LocalTime;
    TIME_FIELDS TimeFields;
    CHAR TimeBuffer[64] = {0}; // Формат: "YYYY-MM-DD HH:MM:SS.XXXXXX"

    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime); // Переводим в локальное время
    RtlTimeToTimeFields(&LocalTime, &TimeFields);

    // Форматируем строку времени
    _snprintf_s(TimeBuffer, sizeof(TimeBuffer), _TRUNCATE,
        "%04d-%02d-%02d %02d:%02d:%02d.%06d",
        TimeFields.Year,
        TimeFields.Month,
        TimeFields.Day,
        TimeFields.Hour,
        TimeFields.Minute,
        TimeFields.Second,
        TimeFields.Milliseconds * 1000);

    if (Process != NULL) {
        PCHAR ImageName = (PCHAR)((ULONG_PTR)Process + 0x5a8);

        __try {
            if (ImageName != NULL) {
                strncpy(ProcessName, ImageName, 15);
                ProcessName[15] = '\0';
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            strcpy(ProcessName, "Unknown");
        }
    }

    switch(NotifyClass) {
        case RegNtPreCreateKeyEx:
            PreCreateInfo = (PREG_CREATE_KEY_INFORMATION) Argument2;
            InfoPrint("[%s] [PID: %d, Process: %s] Callback: Create key %wZ", 
                        TimeBuffer,
                        ProcessId,
                        ProcessName,
                        PreCreateInfo->CompleteName);
            break;

        case RegNtPreSetValueKey:
            PreSetValueInfo = (PREG_SET_VALUE_KEY_INFORMATION)Argument2;

            PUNICODE_STRING KeyName = NULL;
            Status = CmCallbackGetKeyObjectIDEx(
                &g_RegistryCallbackCookie,
                PreSetValueInfo->Object,
                NULL,
                &KeyName,
                0
            );

            if (NT_SUCCESS(Status) && KeyName != NULL) {
                InfoPrint("[%s] [PID: %d, Process: %s] Callback: Set key %wZ value %wZ",
                    TimeBuffer,
                    ProcessId,
                    ProcessName,
                    KeyName,
                    PreSetValueInfo->ValueName);

                // --- Преобразуем KeyName (UNICODE_STRING) в ANSI ---
                ANSI_STRING ansiKeyName = { 0 };
                NTSTATUS convStatus = RtlUnicodeStringToAnsiString(&ansiKeyName, KeyName, TRUE);
                if (NT_SUCCESS(convStatus)) {
                    // Удалось преобразовать юникод имя ключа в ansi строку

                    Status = access_check(ProcessName, ansiKeyName.Buffer);

                    // Обязательно освобождаем память, выделенную RtlUnicodeStringToAnsiString
                    RtlFreeAnsiString(&ansiKeyName);
                } else {
                    // Не удалось преобразовать
                    InfoPrint("F-Callback: Failed to convert key name to ANSI (status: 0x%08X)", convStatus);
                }

                CmCallbackReleaseKeyObjectIDEx(KeyName);
            }
            else {
                InfoPrint("[%s] [PID: %d, Process: %s] Callback: Set value %wZ (key name unavailable)",
                    TimeBuffer,
                    ProcessId,
                    ProcessName,
                    PreSetValueInfo->ValueName);
            }

            break;
            
        default:
            break;
    }

    return Status;
}
