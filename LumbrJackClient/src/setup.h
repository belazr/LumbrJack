#pragma once
#include <Windows.h>

// Handles installing/uninstalling and starting/stopping the driver.
namespace setup {

	// Installs the driver as a service.
	//
	// Parameters:
	// 
	// [in] hScManager:
	// Handle to the service manager. Requires at least SC_MANAGER_CREATE_SERVICE access rights.
	// 
	// [in] name:
	// Name of the created service.
	// 
	// [in] path:
	// Absoulte path of the .sys file.
	// 
	// Return:
	// True on succcess, false on failure.
	bool install(SC_HANDLE hScManager, const char* name, const char* path);

	// Starts the specified service.
	//
	// Parameters:
	// 
	// [in] hScManager:
	// Handle to the service manager. Requires at least SC_MANAGER_CONNECT access rights.
	// 
	// [in] name:
	// Name of the service.
	// 
	// Return:
	// True on succcess, false on failure.
	bool start(SC_HANDLE hScManager, const char* name);

	// Stops the specified service.
	//
	// Parameters:
	// 
	// [in] hScManager:
	// Handle to the service manager. Requires at least SC_MANAGER_CONNECT access rights.
	// 
	// [in] name:
	// Name of the service.
	// 
	// Return:
	// True on succcess, false on failure.
	bool stop(SC_HANDLE hScManager, const char* name);

	// Uninstalls the specified service.
	//
	// Parameters:
	// 
	// [in] hScManager:
	// Handle to the service manager. Requires at least SC_MANAGER_CONNECT access rights.
	// 
	// [in] name:
	// Name of the service.
	// 
	// Return:
	// True on succcess, false on failure.
	bool uninstall(SC_HANDLE hScManager, const char* name);

	// Retrievs the state of the specified service.
	//
	// Parameters:
	// 
	// [in] hScManager:
	// Handle to the service manager. Requires at least SC_MANAGER_CONNECT access rights.
	// 
	// [in] name:
	// Name of the service.
	// 
	// Return:
	// The current state of the service on success, 0 on failure.
	DWORD getState(SC_HANDLE hScManager, const char* name);

}