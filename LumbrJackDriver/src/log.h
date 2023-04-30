#pragma once
#include "BlockingQueue.h"
#include <Ntddkbd.h>
#include <Ntddmou.h>

typedef enum LogType {
	LOG_KBD, LOG_MOU, LOG_MAX
}LogType;

// Structure for a doubly linked list containing KEYBOARD_INPUT_DATA for the blocking queue.
typedef struct KbdDataEntry {
	LIST_ENTRY list;
	KEYBOARD_INPUT_DATA data;
}KbdDataEntry;

// Structure for a doubly linked list containing MOUSE_INPUT_DATA for the blocking queue.
typedef struct MouDataEntry {
	LIST_ENTRY list;
	MOUSE_INPUT_DATA data;
}MouDataEntry;

// Logging thread objects.
extern PKTHREAD pLogThreads[LOG_MAX];

// Blocking queues to process input data.
extern BlockingQueue inputQueues[LOG_MAX];

// Starts a logging thread.
// The driver will not unload before this thread has not finished.
//
// Parameters:
//
// [in] pDriverObject:
// Address of the driver object of the current driver.
// 
// [in] type:
// The logging type that should be performed by the thread.
// 
// Return:
// An appropriate NTSTATUS value.
NTSTATUS startLogThread(PDRIVER_OBJECT pDriverObject, LogType type);

// Stops a logging thread by sending a dummy item to the blocking queue.
//
// Parameters:
//
// [in] type:
// The logging type of the thread.
//
// Return:
// An appropriate NTSTATUS value.
NTSTATUS stopLogThread(LogType type);

// Logs a KEYBOARD_INPUT_DATA stucture to the debugger.
//
// Parameters:
// 
// [in] pKbdInputData:
// Address of the KEYBOARD_INPUT_DATA stucture to log.
void logKbdToDbg(PKEYBOARD_INPUT_DATA pKbdInputData);

// Logs a MOUSE_INPUT_DATA stucture to the debugger.
// Only logs if either the left or right mouse button get pressed or released.
//
// Parameters:
// 
// [in] pMouInputData:
// Address of the KEYBOARD_INPUT_DATA stucture to log.
void logMouToDbg(PMOUSE_INPUT_DATA pMouInputData);