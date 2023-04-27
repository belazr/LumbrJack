#pragma once
#include <ntddk.h>

// Blocking queue implementation for producer-consumer problem when writing keyboard input to a file

typedef struct BlockingQueue {
	KSEMAPHORE semaphoreAdd;
	KSEMAPHORE semaphoreRemove;
	KSPIN_LOCK spinLock;
	LIST_ENTRY head;
	ULONGLONG size;
	BOOLEAN isWaiting;
}BlockingQueue;

// Initializes the blocking queue.
//
// Parameters:
// 
// [out] pBlockingQueue:
// Contains an initialized blocking queue on return.
//
// [in] maxSize:
// Maximum capacity of the blocking queue
void initBlockingQueue(BlockingQueue* pBlockingQueue, LONG maxSize);

// Adds an item to the blocking queue.
// If the queue is full the calling thread will wait if IRQL < DISPTACH_LEVEL.
// For IRQL == DISPATCH_LEVEL the function times out immediately without adding the item.
// For IRQL > DISPATCH_LEVEL it fails without adding the item.
// This is due to limitations of KeWaitForSingleObject.
// 
// Parameters:
// 
// [in/out] pBlockingQueue:
// Address of the blocking queue to which the item should be added.
// 
// [in]
// Address of the ListEntry struct of the item to add.
NTSTATUS addToBlockigQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY* pListEntry);

// Removes an item from the blocking queue.
// If the queue is empty the calling thread will wait if IRQL < DISPTACH_LEVEL.
// For IRQL == DISPATCH_LEVEL the function times out immediately without removing the item.
// For IRQL > DISPATCH_LEVEL the function fails without removing the item.
// This is due to limitations of KeWaitForSingleObject.
// 
// Parameters:
// 
// [in/out] pBlockingQueue:
// Address of the blocking queue from which the item should be removed.
// 
// [out]
// Contains the Address of the ListEntry struct of the removed item on return.
NTSTATUS removeFromBlockingQueue(BlockingQueue* pBlockingQueue, LIST_ENTRY** ppListEntry);

