#include "setup.h"

namespace setup {

	bool install(SC_HANDLE hScManager, const char* name, const char* path) {
		SC_HANDLE hService = CreateServiceA(hScManager, name, name, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, path, NULL, NULL, NULL, NULL, NULL);

		if (!hService) {

			return false;
		}

		CloseServiceHandle(hService);

		return true;
	}


	bool start(SC_HANDLE hScManager, const char* name) {
		SC_HANDLE hService = OpenServiceA(hScManager, name, SERVICE_START);

		if (!hService) {
			
			return false;
		}

		if (!StartServiceA(hService, 0, NULL)) {
			CloseServiceHandle(hService);
			
			return false;
		}

		CloseServiceHandle(hService);

		return true;
	}


	bool stop(SC_HANDLE hScManager, const char* name) {
		SC_HANDLE hService = OpenServiceA(hScManager, name, SERVICE_STOP);

		if (!hService) {
			return false;
		}

		SERVICE_STATUS scStatus{};

		if (!ControlService(hService, SERVICE_CONTROL_STOP, &scStatus)) {
			CloseServiceHandle(hService);

			return false;
		}

		CloseServiceHandle(hService);

		return true;
	}


	bool uninstall(SC_HANDLE hScManager, const char* name) {
		SC_HANDLE hService = OpenServiceA(hScManager, name, DELETE);

		if (!hService) {
			return false;
		}

		if (!DeleteService(hService)) {
			CloseServiceHandle(hService);

			return false;
		}

		CloseServiceHandle(hService);

		return true;
	}


	DWORD getState(SC_HANDLE hScManager, const char* name) {
		SC_HANDLE hService = OpenServiceA(hScManager, name, SERVICE_QUERY_STATUS);

		if (!hService) {
			return 0;
		}

		SERVICE_STATUS scStatus{};

		if (!QueryServiceStatus(hService, &scStatus)) {
			CloseServiceHandle(hService);

			return 0;
		}

		CloseServiceHandle(hService);

		return scStatus.dwCurrentState;
	}

}