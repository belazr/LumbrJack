#include "setup.h"
#include "requests.h"
#include "io.h"
#include <iostream>

#define DRIVER_NAME "LumbrJackDriver"
#define SYM_LINK_NAME "\\\\.\\LumbrJackDevSymLink"

static void takeSetupAction(io::action curAction, const std::string* pDriverPath);
static void takeIoAction(io::action curAction);

int main(int argc, char* argv[]) {
    std::string driverPath;

    if (argc != 2) {
        std::cout << "Please specify the location of the .sys file of the driver." << std::endl;

        return 0;
    }
    else {
        driverPath = argv[1];
    }

    io::action curAction = io::action::START;

    while (curAction != io::action::EXIT) {
        const SC_HANDLE hScManager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        DWORD driverState = 0;

        if (hScManager) {
            driverState = setup::getState(hScManager, DRIVER_NAME);
            CloseServiceHandle(hScManager);
        }

        
        io::printMenu(curAction, driverState);
        io::selectAction(&curAction);
        
        switch (curAction) {
        case io::action::INSTALL:
        case io::action::START:
        case io::action::STOP:
        case io::action::UNINSTALL:
            takeSetupAction(curAction, &driverPath);
            break;
        case io::action::LOG_STATE:
        case io::action::LOG_START:
        case io::action::LOG_STOP:
            takeIoAction(curAction);
            break;
        default:
            break;
        }

    }

    return 0;
}


static void takeSetupAction(io::action curAction, const std::string* pDriverPath) {
    SC_HANDLE hScManager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

    if (!hScManager) {
        std::cout << "Failed to open service manager.";

        return;
    }

    switch (curAction) {
    case io::action::INSTALL:

        if (setup::install(hScManager, DRIVER_NAME, pDriverPath->c_str())) {
            std::cout << "Driver installed." << std::endl;
        }
        else {
            std::cout << "Failed to install." << std::endl;
        }

        break;
    case io::action::START:

        if (setup::start(hScManager, DRIVER_NAME)) {
            std::cout << "Driver started." << std::endl;
        }
        else {
            std::cout << "Failed to start." << std::endl;
        }

        break;
    case io::action::STOP:

        if (setup::stop(hScManager, DRIVER_NAME)) {
            std::cout << "Driver stoped." << std::endl;
        }
        else {
            std::cout << "Failed to stop." << std::endl;
        }

        break;
    case io::action::UNINSTALL:

        if (setup::uninstall(hScManager, DRIVER_NAME)) {
            std::cout << "Driver uninstalled." << std::endl;
        }
        else {
            std::cout << "Failed to uninstall." << std::endl;
        }

        break;
    default:
        std::cout << "Invalid setup action." << std::endl;
        break;
    }

    std::cout << std::endl;

    CloseServiceHandle(hScManager);
    
    return;
}


static void takeIoAction(io::action curAction) {
    const HANDLE hDevice = CreateFileA(SYM_LINK_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);

    if (!hDevice || hDevice == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to get device handle." << std::endl;
        std::cout << std::endl;
        
        return;
    }

    bool isLogging = false;

    switch (curAction) {
    case io::action::LOG_STATE:

        if (requests::getLoggingState(hDevice, &isLogging)) {

            if (isLogging) {
                std::cout << "Driver is logging." << std::endl;
            }
            else {
                std::cout << "Driver is not logging." << std::endl;
            }

        }
        else {
            std::cout << "Failed to get logging state." << std::endl;
        }

        break;
    case io::action::LOG_START:
        
        if (requests::startLogging(hDevice)) {
            std::cout << "Driver started logging." << std::endl;
        }
        else {
            std::cout << "Failed to start logging." << std::endl;
        }

        break;
    case io::action::LOG_STOP:

        if (requests::stopLogging(hDevice)) {
            std::cout << "Driver stopped logging." << std::endl;
        }
        else {
            std::cout << "Failed to stop logging." << std::endl;
        }
        
        break;
    default:
        break;
    }

    std::cout << std::endl;

    CloseHandle(hDevice);

    return;
}