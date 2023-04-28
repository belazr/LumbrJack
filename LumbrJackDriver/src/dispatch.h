#pragma once
#include "log.h"
#include <ntddk.h>

// Contains the DRIVER_DISPATCH callback functions.

// Indicates if logging of keypresses is switched on or off.
extern BOOLEAN isLogging;
// Semaphores to check if read opreation has finished.
extern KSEMAPHORE readSemaphores[LOG_MAX];

// Finishes a request or passes it through to the next driver in the stack.
//
// Parameters:
// [in] pDeviceObject:
// Caller-supplied pointer to a DEVICE_OBJECT structure.
//
// [in/out] pIrp:
// Address of the IRP describing the requested I/O operation.
// 
// Return:
// An appropriate NTSTATUS value.
NTSTATUS LmbPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

// Finishes an device control request from the client application indicated by the IOCTL code.
// Either returns the current logging state, or switches the logging on or off.
//
// Parameters:
// [in] pDeviceObject:
// Caller-supplied pointer to a DEVICE_OBJECT structure.
//
// [in/out] pIrp:
// Address of the IRP describing the requested I/O operation.
// 
// Return:
// An appropriate NTSTATUS value.
NTSTATUS LmbDispatchDeviceControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

// Passes a read request to the next driver in the stack.
//
// Parameters:
// [in] pDeviceObject:
// Caller-supplied pointer to a DEVICE_OBJECT structure.
//
// [in/out] pIrp:
// Address of the IRP describing the requested I/O operation.
// 
// Return:
// An appropriate NTSTATUS value.
NTSTATUS LmbDispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
