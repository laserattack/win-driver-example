/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    regfltr.c

Abstract: 

    Sample driver used to run the kernel mode registry callback samples.

Environment:

    Kernel mode only

--*/

#include "regfltr.h"


//
// The root key used in the samples
//
HANDLE g_RootKey;


LPCWSTR 
GetNotifyClassString (
    _In_ REG_NOTIFY_CLASS NotifyClass
    );

VOID
DeleteTestKeys(
    );



NTSTATUS 
Callback (
    _In_     PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
)

{

    NTSTATUS Status = STATUS_SUCCESS;
    REG_NOTIFY_CLASS NotifyClass;
    PCALLBACK_CONTEXT CallbackCtx;

    CallbackCtx = (PCALLBACK_CONTEXT)CallbackContext;
    NotifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;

    InfoPrint("\tCallback: Altitude-%S, NotifyClass-%S.",
               CallbackCtx->AltitudeBuffer,
               GetNotifyClassString(NotifyClass));

    if (Argument2 == NULL) {

        ErrorPrint("\tCallback: Argument 2 unexpectedly 0. Filter will "
                    "abort and return success.");
        return STATUS_SUCCESS;
    }
    
    switch (CallbackCtx->CallbackMode) {
        case CALLBACK_MODE_PRE_NOTIFICATION_LOG:
            Status = CallbackPreNotificationLog(CallbackCtx, NotifyClass, Argument2);
            break;
        default: 
            ErrorPrint("Unknown Callback Mode: %d", CallbackCtx->CallbackMode);
            Status = STATUS_SUCCESS;
    }


    return Status;
    
}

NTSTATUS 
DoCallbackSamples(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    ULONG OutputBufferLength;
    PDO_KERNELMODE_SAMPLES_OUTPUT Output;

    UNREFERENCED_PARAMETER(DeviceObject);

    //
    // Get the output buffer from the irp and check it is as large as expected.
    //
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    OutputBufferLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof (DO_KERNELMODE_SAMPLES_OUTPUT)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Output = (PDO_KERNELMODE_SAMPLES_OUTPUT) Irp->AssociatedIrp.SystemBuffer;

    //
    // Call each demo and record the results in the Output->SampleResults
    // array
    //

    Output->SampleResults[KERNELMODE_SAMPLE_PRE_NOTIFICATION_LOG] =
        PreNotificationLogSample();

    Irp->IoStatus.Information = sizeof(DO_KERNELMODE_SAMPLES_OUTPUT);

    LoadImageNotifySample();


  Exit:

    InfoPrint("");
    InfoPrint("Kernel Mode Samples End");
    InfoPrint("");
    
    return Status;
}


NTSTATUS
GetCallbackVersion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    ) 
/*++

Routine Description:

    Calls CmGetCallbackVersion

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    ULONG OutputBufferLength;
    PGET_CALLBACK_VERSION_OUTPUT GetCallbackVersionOutput;

    UNREFERENCED_PARAMETER(DeviceObject);

    //
    // Get the output buffer and verify its size
    //
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    OutputBufferLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof(GET_CALLBACK_VERSION_OUTPUT)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    GetCallbackVersionOutput = (PGET_CALLBACK_VERSION_OUTPUT) Irp->AssociatedIrp.SystemBuffer;

    //
    // Call CmGetCallbackVersion and store the results in the output buffer
    //
    
    CmGetCallbackVersion(&GetCallbackVersionOutput->MajorVersion, 
                         &GetCallbackVersionOutput->MinorVersion);   

    Irp->IoStatus.Information = sizeof(GET_CALLBACK_VERSION_OUTPUT);

  Exit:

    if (!NT_SUCCESS(Status)) {
        ErrorPrint("GetCallbackVersion failed. Status 0x%x", Status);
    } else {
        InfoPrint("GetCallbackVersion succeeded");
    }

    return Status;
}


LPCWSTR 
GetNotifyClassString (
    _In_ REG_NOTIFY_CLASS NotifyClass
    )
/*++

Routine Description:

    Converts from NotifyClass to a string

Arguments:

    NotifyClass - value that identifies the type of registry operation that 
        is being performed

Return Value:

    Returns a string of the name of NotifyClass.
    
--*/
{
    switch (NotifyClass) {
        case RegNtPreDeleteKey:                 return L"RegNtPreDeleteKey";
        case RegNtPreSetValueKey:               return L"RegNtPreSetValueKey";
        case RegNtPreDeleteValueKey:            return L"RegNtPreDeleteValueKey";
        case RegNtPreSetInformationKey:         return L"RegNtPreSetInformationKey";
        case RegNtPreRenameKey:                 return L"RegNtPreRenameKey";
        case RegNtPreEnumerateKey:              return L"RegNtPreEnumerateKey";
        case RegNtPreEnumerateValueKey:         return L"RegNtPreEnumerateValueKey";
        case RegNtPreQueryKey:                  return L"RegNtPreQueryKey";
        case RegNtPreQueryValueKey:             return L"RegNtPreQueryValueKey";
        case RegNtPreQueryMultipleValueKey:     return L"RegNtPreQueryMultipleValueKey";
        case RegNtPreKeyHandleClose:            return L"RegNtPreKeyHandleClose";
        case RegNtPreCreateKeyEx:               return L"RegNtPreCreateKeyEx";
        case RegNtPreOpenKeyEx:                 return L"RegNtPreOpenKeyEx";
        case RegNtPreFlushKey:                  return L"RegNtPreFlushKey";
        case RegNtPreLoadKey:                   return L"RegNtPreLoadKey";
        case RegNtPreUnLoadKey:                 return L"RegNtPreUnLoadKey";
        case RegNtPreQueryKeySecurity:          return L"RegNtPreQueryKeySecurity";
        case RegNtPreSetKeySecurity:            return L"RegNtPreSetKeySecurity";
        case RegNtPreRestoreKey:                return L"RegNtPreRestoreKey";
        case RegNtPreSaveKey:                   return L"RegNtPreSaveKey";
        case RegNtPreReplaceKey:                return L"RegNtPreReplaceKey";

        case RegNtPostDeleteKey:                return L"RegNtPostDeleteKey";
        case RegNtPostSetValueKey:              return L"RegNtPostSetValueKey";
        case RegNtPostDeleteValueKey:           return L"RegNtPostDeleteValueKey";
        case RegNtPostSetInformationKey:        return L"RegNtPostSetInformationKey";
        case RegNtPostRenameKey:                return L"RegNtPostRenameKey";
        case RegNtPostEnumerateKey:             return L"RegNtPostEnumerateKey";
        case RegNtPostEnumerateValueKey:        return L"RegNtPostEnumerateValueKey";
        case RegNtPostQueryKey:                 return L"RegNtPostQueryKey";
        case RegNtPostQueryValueKey:            return L"RegNtPostQueryValueKey";
        case RegNtPostQueryMultipleValueKey:    return L"RegNtPostQueryMultipleValueKey";
        case RegNtPostKeyHandleClose:           return L"RegNtPostKeyHandleClose";
        case RegNtPostCreateKeyEx:              return L"RegNtPostCreateKeyEx";
        case RegNtPostOpenKeyEx:                return L"RegNtPostOpenKeyEx";
        case RegNtPostFlushKey:                 return L"RegNtPostFlushKey";
        case RegNtPostLoadKey:                  return L"RegNtPostLoadKey";
        case RegNtPostUnLoadKey:                return L"RegNtPostUnLoadKey";
        case RegNtPostQueryKeySecurity:         return L"RegNtPostQueryKeySecurity";
        case RegNtPostSetKeySecurity:           return L"RegNtPostSetKeySecurity";
        case RegNtPostRestoreKey:               return L"RegNtPostRestoreKey";
        case RegNtPostSaveKey:                  return L"RegNtPostSaveKey";
        case RegNtPostReplaceKey:               return L"RegNtPostReplaceKey";

        case RegNtCallbackObjectContextCleanup: return L"RegNtCallbackObjectContextCleanup";

        default:
            return L"Unsupported REG_NOTIFY_CLASS";
    }
}
