#include "BlockingQueue.h"
#include "debug.h"

void initBlockingQueue(BlockingQueue* pBlockingQueue, LONG maxSize) {
	KeInitializeSemaphore(&pBlockingQueue->semaphoreAdd, maxSize, maxSize);
	KeInitializeSemaphore(&pBlockingQueue->semaphoreRemove, 0, maxSize);
	KeInitializeSpinLock(&pBlockingQueue->spinLock);
	InitializeListHead(&pBlockingQueue->head);
	pBlockingQueue->size = 0;
	pBlockingQueue->isWaiting = TRUE;
}


NTSTATUS addToBlockigQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY* pListEntry) {
	NTSTATUS ntStatus = STATUS_SUCCESS;
	const KIRQL curIrql = KeGetCurrentIrql();

	if (curIrql == DISPATCH_LEVEL) {
		// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
		LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreAdd, Executive, KernelMode, FALSE, &zeroTimeout);
		
		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("addToBlockigQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}
		else if (ntStatus == STATUS_TIMEOUT) {
			DBG_PRINT("addToBlockigQueue: KeWaitForSingleObject timeout\n");

			return ntStatus;
		}

		KeAcquireSpinLockAtDpcLevel(&pBlockingQueue->spinLock);
		InsertTailList(&pBlockingQueue->head, pListEntry);
		pBlockingQueue->size++;
		KeReleaseSpinLockFromDpcLevel(&pBlockingQueue->spinLock);

		KeReleaseSemaphore(&pBlockingQueue->semaphoreRemove, 0, 1, FALSE);
	}
	else if (curIrql < DISPATCH_LEVEL) {
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreAdd, Executive, KernelMode, FALSE, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("addToBlockigQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		KeAcquireSpinLockRaiseToDpc(&pBlockingQueue->spinLock);
		InsertTailList(&pBlockingQueue->head, pListEntry);
		pBlockingQueue->size++;
		KeReleaseSpinLock(&pBlockingQueue->spinLock, curIrql);

		KeReleaseSemaphore(&pBlockingQueue->semaphoreRemove, 0, 1, FALSE);
	}
	else {
		DBG_PRINT("addToBlockigQueue: IRQL too high\n");
	}

	return ntStatus;
}


NTSTATUS removeFromBlockingQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY** ppListEntry) {
	NTSTATUS ntStatus = STATUS_SUCCESS;
	const KIRQL curIrql = KeGetCurrentIrql();

	if (curIrql == DISPATCH_LEVEL) {
		// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
		LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreRemove, Executive, KernelMode, FALSE, &zeroTimeout);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("removeFromBlockingQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}
		else if (ntStatus == STATUS_TIMEOUT) {
			DBG_PRINT("removeFromBlockingQueue: KeWaitForSingleObject timeout\n");

			return ntStatus;
		}

		KeAcquireSpinLockAtDpcLevel(&pBlockingQueue->spinLock);
		*ppListEntry = RemoveHeadList(&pBlockingQueue->head);
		pBlockingQueue->size--;
		KeReleaseSpinLockFromDpcLevel(&pBlockingQueue->spinLock);

		KeReleaseSemaphore(&pBlockingQueue->semaphoreAdd, 0, 1, FALSE);
	}
	else if (curIrql < DISPATCH_LEVEL) {
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreRemove, Executive, KernelMode, FALSE, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("removeFromBlockingQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		KeAcquireSpinLockRaiseToDpc(&pBlockingQueue->spinLock);
		*ppListEntry = RemoveHeadList(&pBlockingQueue->head);
		pBlockingQueue->size--;
		KeReleaseSpinLock(&pBlockingQueue->spinLock, curIrql);

		KeReleaseSemaphore(&pBlockingQueue->semaphoreAdd, 0, 1, FALSE);
	}
	else {
		DBG_PRINT("removeFromBlockingQueue: IRQL too high\n");
	}

	return ntStatus;
}