#pragma once
#include <ntddk.h>

#define DBG_PRINT(f) KdPrintEx((DPFLTR_IHVDRIVER_ID, 0, f))
#define DBG_PRINTF(f, x) KdPrintEx((DPFLTR_IHVDRIVER_ID, 0, f, x))
