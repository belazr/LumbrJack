#pragma once
#include <ntddk.h>

// Tags for dynamic memory allocations.
#define LOG_THREAD_DATA_TAG 'LTHD'
#define KBD_LIST_DATA_TAG 'KBLD'
#define MOU_LIST_DATA_TAG 'MOLD'

#define DBG_PRINT(f) KdPrintEx((DPFLTR_IHVDRIVER_ID, 0, f))
#define DBG_PRINTF(f, x) KdPrintEx((DPFLTR_IHVDRIVER_ID, 0, f, x))
#define DBG_PRINTF2(f, x, y) KdPrintEx((DPFLTR_IHVDRIVER_ID, 0, f, x, y))
