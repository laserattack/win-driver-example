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

    switch(NotifyClass) {
        case RegNtPreCreateKeyEx:
            PreCreateInfo = (PREG_CREATE_KEY_INFORMATION) Argument2;
            InfoPrint("[PID: %d] Callback: Create key %wZ.", 
                        ProcessId,
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
                InfoPrint("[PID: %d] Callback: Set key %wZ value %wZ.",
                    ProcessId,
                    KeyName,
                    PreSetValueInfo->ValueName);

                CmCallbackReleaseKeyObjectIDEx(KeyName);
            }
            else {
                InfoPrint("[PID: %d] Callback: Set value %wZ (key name unavailable).",
                    ProcessId,
                    PreSetValueInfo->ValueName);
            }

            break;
            
        default:
            //
            // Do nothing for other notifications
            //
            break;
    }

    return Status;
}
