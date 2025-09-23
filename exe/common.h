/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    Common.h

Abstract: 

    Definitions common to both the driver and the executable.
    
Environment:

    User and kernel mode 
    
--*/

#pragma once

//
// Driver and device names.
//

#define DRIVER_NAME             L"RegFltr"
#define DRIVER_NAME_WITH_EXT    L"RegFltr.sys"

#define NT_DEVICE_NAME          L"\\Device\\RegFltr"
#define DOS_DEVICES_LINK_NAME   L"\\DosDevices\\RegFltr"
#define WIN32_DEVICE_NAME       L"\\\\.\\RegFltr"

//
// SDDL string used when creating the device. This string
// limits access to this driver to system and admins only.
//

#define DEVICE_SDDL             L"D:P(A;;GA;;;SY)(A;;GA;;;BA)"

//
// IOCTLs exposed by the driver.
//

#define IOCTL_DO_KERNELMODE_SAMPLES    CTL_CODE (FILE_DEVICE_UNKNOWN, (0x800 + 0), METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_CALLBACK_VERSION     CTL_CODE (FILE_DEVICE_UNKNOWN, (0x800 + 3), METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

//
// Common definitions
// 

#define CALLBACK_LOW_ALTITUDE      L"380000"
#define CALLBACK_ALTITUDE          L"380010"
#define CALLBACK_HIGH_ALTITUDE     L"380020"

#define MAX_ALTITUDE_BUFFER_LENGTH 10

//
// List of callback modes
//
typedef enum _CALLBACK_MODE {
    CALLBACK_MODE_PRE_NOTIFICATION_LOG,
} CALLBACK_MODE;

//
// List of kernel mode samples
//
typedef enum _KERNELMODE_SAMPLE {
    KERNELMODE_SAMPLE_PRE_NOTIFICATION_LOG = 0,
    MAX_KERNELMODE_SAMPLES
} KERNELMODE_SAMPLE;


typedef struct _GET_CALLBACK_VERSION_OUTPUT {

    //
    // Receives the version number of the registry callback
    //
    ULONG MajorVersion;
    ULONG MinorVersion;
    
} GET_CALLBACK_VERSION_OUTPUT, *PGET_CALLBACK_VERSION_OUTPUT;


typedef struct _DO_KERNELMODE_SAMPLES_OUTPUT {

    //
    // An array that receives the results of the kernel mode samples.
    //
    BOOLEAN SampleResults[MAX_KERNELMODE_SAMPLES];
    
} DO_KERNELMODE_SAMPLES_OUTPUT, *PDO_KERNELMODE_SAMPLES_OUTPUT;


