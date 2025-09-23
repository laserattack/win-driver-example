/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    regfltr.h

Abstract: 

    Header file for the sample driver

Environment:

    Kernel mode only


--*/

#pragma once

#include <ntifs.h>
#include <ntstrsafe.h>
#include <wdmsec.h>

#include "common.h"


//
// Pool tags
//

#define REGFLTR_CONTEXT_POOL_TAG          '0tfR'
#define REGFLTR_CAPTURE_POOL_TAG          '1tfR'


//
// Logging macros
//

#define InfoPrint(str, ...)                 \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,         \
               DPFLTR_INFO_LEVEL,           \
               "%S: "##str"\n",             \
               DRIVER_NAME,                 \
               __VA_ARGS__)

#define ErrorPrint(str, ...)                \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,         \
               DPFLTR_ERROR_LEVEL,          \
               "%S: %d: "##str"\n",         \
               DRIVER_NAME,                 \
               __LINE__,                    \
               __VA_ARGS__)


extern LARGE_INTEGER g_RegistryCallbackCookie;
extern BOOLEAN g_IsCallbackRegistered;


//
// The root key used in the samples
//
extern HANDLE g_RootKey;


//
// Pointer to the device object used to register registry callbacks
//
extern PDEVICE_OBJECT g_DeviceObj;


//
// Registry callback version
//
extern ULONG g_MajorVersion;
extern ULONG g_MinorVersion;


//
// Set to TRUE if TM and RM were successfully created and the transaction
// callback was successfully enabled. 
//
extern BOOLEAN g_RMCreated;


//
// Flag that indicates if the system is win8 or higher. This is set on
// driver entry by calling RtlVerifyVersionInfo.
//
extern BOOLEAN g_IsWin8OrGreater;


//
// The following are variables used to manage callback contexts handed
// out to user mode.
//

#define MAX_CALLBACK_CTX_ENTRIES            10

//
// The fast mutex guarding the callback context list
//
extern FAST_MUTEX g_CallbackCtxListLock;

//
// The list head
//
extern LIST_ENTRY g_CallbackCtxListHead;

//
// Count of entries in list
//
extern USHORT g_NumCallbackCtxListEntries;

//
// Context data structure for the transaction callback RMCallback
//

typedef struct _RMCALLBACK_CONTEXT {

    //
    // A bit mask of all transaction notifications types that the RM Callback is 
    // notified of.
    //
    ULONG Notification;

    //
    // The handle to an enlistment
    //
    HANDLE Enlistment;
    
} RMCALLBACK_CONTEXT, *PRMCALLBACK_CONTEXT;


//
// The context data structure for the registry callback. It will be passed 
// to the callback function every time it is called. 
//

typedef struct _CALLBACK_CONTEXT {

    //
    // List of callback contexts currently active
    //
    LIST_ENTRY CallbackCtxList;

    //
    // Specifies which callback helper method to use
    //
    CALLBACK_MODE CallbackMode;

    //
    // Records the current ProcessId to filter out registry operation from
    // other processes.
    //
    HANDLE ProcessId;

    //
    // Records the altitude that the callback was registered at
    //
    UNICODE_STRING Altitude; 
    WCHAR AltitudeBuffer[MAX_ALTITUDE_BUFFER_LENGTH];
        
    //
    // Records the cookie returned by the registry when the callback was 
    // registered
    //
    LARGE_INTEGER Cookie;

    //
    // A pointer to the context for the transaction callback. 
    // Used to enlist on a transaction. Only used in the transaction samples.
    //
    PRMCALLBACK_CONTEXT RMCallbackCtx;

    //
    // These fields record information for verifying the behavior of the
    // certain samples. They are not used in all samples
    //
    
    //
    // Number of times the RegNtCallbackObjectContextCleanup 
    // notification was received
    //
    LONG ContextCleanupCount;

    //
    // Number of times the callback saw a notification with the call or
    // object context set correctly.
    //
    LONG NotificationWithContextCount;

    //
    // Number of times callback saw a notirication without call or without
    // object context set correctly
    //
    LONG NotificationWithNoContextCount;

    //
    // Number of pre-notifications received
    //
    LONG PreNotificationCount;

    //
    // Number of post-notifications received
    //
    LONG PostNotificationCount;
    
} CALLBACK_CONTEXT, *PCALLBACK_CONTEXT;


//
// The registry and transaction callback routines
//

EX_CALLBACK_FUNCTION Callback;

//
// The samples and their corresponding callback helper methods
//

NTSTATUS 
CallbackPreNotificationLog(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _Inout_ PVOID Argument2
    );

BOOLEAN 
PreNotificationLogSample();

//
// Driver dispatch functions
//

NTSTATUS
DoCallbackSamples(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    );

NTSTATUS
GetCallbackVersion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    );

//
// Utility methods
//

PVOID
CreateCallbackContext(
    _In_ CALLBACK_MODE CallbackMode, 
    _In_ PCWSTR AltitudeString
    );