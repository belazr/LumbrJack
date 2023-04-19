#pragma once
#include <Windows.h>

// Handles interaction with the driver.

namespace requests {

	// Checks if the driver is currently logging.
	//
	// Parameters:
	// [in] hDevice:
	// Handle to the communication device of the driver.
	// 
	// [out] 
	// pIsLogging:
	// Contains the logging state of the driver on return.
	//
	// Return:
	// True on succcess, false on failure.
	bool getLoggingState(HANDLE hDevice, bool* pIsLogging);

	// Starts logging in the driver.
	//
	// Parameters:
	// [in] hDevice:
	// Handle to the communication device of the driver.
	//
	// Return:
	// True on succcess, false on failure.
	bool startLogging(HANDLE hDevice);

	// Stops logging in the driver.
	//
	// Parameters:
	// [in] hDevice:
	// Handle to the communication device of the driver.
	//
	// Return:
	// True on succcess, false on failure.
	bool stopLogging(HANDLE hDevice);

}