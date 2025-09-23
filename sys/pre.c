/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    Pre.c

Abstract: 

    Samples that show what callbacks can do during the pre-notification
    phase.

Environment:

    Kernel mode only

--*/

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
