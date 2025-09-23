/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    util.c

Abstract: 

    Utility routines for the sample driver.

Environment:

    Kernel mode only

--*/


#include "regfltr.h"


FAST_MUTEX g_CallbackCtxListLock;
LIST_ENTRY g_CallbackCtxListHead;
USHORT g_NumCallbackCtxListEntries;


PVOID
CreateCallbackContext(
    _In_ CALLBACK_MODE CallbackMode,
    _In_ PCWSTR AltitudeString
    ) 
/*++

Routine Description:

    Utility method to create a callback context. Callback context 
    should be freed using DeleteCallbackContext.
    
Arguments:

    CallbackMode - the callback mode value

    AltitudeString - a string with the altitude the callback will be 
        registered at

Return Value:

    Pointer to the allocated and initialized callback context 

--*/
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