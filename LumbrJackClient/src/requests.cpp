#include "requests.h"
#include  "..\..\LumbrJackDriver\src\ioctl.h"

namespace requests {

    bool getLoggingState(HANDLE hDevice, bool* pIsLogging) {

        if (!DeviceIoControl(hDevice, IOCTL_SEND_LOG_STATE, nullptr, 0, pIsLogging, sizeof(*pIsLogging), nullptr, nullptr)) return false;

        return true;
    }


    bool startLogging(HANDLE hDevice) {

        if (!DeviceIoControl(hDevice, IOCTL_LOG_START, nullptr, 0, nullptr, 0, nullptr, nullptr)) return false;

        return true;
    }

    bool stopLogging(HANDLE hDevice) {

        if (!DeviceIoControl(hDevice, IOCTL_LOG_STOP, nullptr, 0, nullptr, 0, nullptr, nullptr)) return false;

        return true;
    }

}