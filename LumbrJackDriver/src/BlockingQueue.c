#include "BlockingQueue.h"
#include "debug.h"

void initBlockingQueue(BlockingQueue* pBlockingQueue, LONG maxSize) {
	KeInitializeSemaphore(&pBlockingQueue->semaphoreAdd, maxSize, maxSize);
	KeInitializeSemaphore(&pBlockingQueue->semaphoreRemove, 0, maxSize);
	InitializeListHead(&pBlockingQueue->head);
	pBlockingQueue->size = 0;
	pBlockingQueue->isWaiting = TRUE;
}


NTSTATUS addToBlockigQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY* pListEntry) {
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;

	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	if (KeGetCurrentIrql() == DISPATCH_LEVEL) {
		LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreAdd, Executive, KernelMode, FALSE, &zeroTimeout);
		
		if (ntStatus == STATUS_TIMEOUT) {
			DBG_PRINT("addToBlockigQueue: KeWaitForSingleObject timeout\n");

			return ntStatus;
		}

	}
	else if (KeGetCurrentIrql() < DISPATCH_LEVEL) {
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreAdd, Executive, KernelMode, FALSE, NULL);
	}
	else {
		DBG_PRINT("addToBlockigQueue: IRQL too high\n");

		return ntStatus;
	}

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("addToBlockigQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}
	
	InsertTailList(&pBlockingQueue->head, pListEntry);
	pBlockingQueue->size++;
	KeReleaseSemaphore(&pBlockingQueue->semaphoreRemove, 0, 1, FALSE);

	return ntStatus;
}


NTSTATUS removeFromBlockingQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY** ppListEntry) {
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;

	// timeout needs to be zero at IRQL >= DISPATCH_LEVEL
	if (KeGetCurrentIrql() == DISPATCH_LEVEL) {
		LARGE_INTEGER zeroTimeout = { .QuadPart = 0 };
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreRemove, Executive, KernelMode, FALSE, &zeroTimeout);

		if (ntStatus == STATUS_TIMEOUT) {
			DBG_PRINT("removeFromBlockingQueue: KeWaitForSingleObject timeout\n");

			return ntStatus;
		}

	}
	else if (KeGetCurrentIrql() < DISPATCH_LEVEL) {
		ntStatus = KeWaitForSingleObject(&pBlockingQueue->semaphoreRemove, Executive, KernelMode, FALSE, NULL);
	}
	else {
		DBG_PRINT("removeFromBlockingQueue: IRQL too high\n");

		return ntStatus;
	}

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("removeFromBlockingQueue: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	*ppListEntry = RemoveHeadList(&pBlockingQueue->head);
	pBlockingQueue->size--;
	KeReleaseSemaphore(&pBlockingQueue->semaphoreAdd, 0, 1, FALSE);

	return ntStatus;
}