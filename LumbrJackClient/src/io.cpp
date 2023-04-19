#include "io.h"
#include <unordered_map>
#include <string>
#include <iostream>

namespace io {
    
    // lables for actions
    static std::unordered_map<action, std::string> actionStrings{
    { action::INSTALL, "Install"},
    { action::START, "Start"},
    { action::STOP, "Stop"},
    { action::UNINSTALL, "Remove"},
    { action::LOG_STATE, "Get loggin state"},
    { action::LOG_START, "Start logging"},
    { action::LOG_STOP, "Stop logging"},
    { action::EXIT, "Exit"}
    };


    static std::string getActionString(action act, bool isSelected);

    void printMenu(action select, DWORD driverState) {

        for (int i = action::INSTALL; i < action::MAX_ACTION; i++) {
            std::cout << getActionString(static_cast<action>(i), i == select) << std::endl;
        }

        std::cout << getActionString(action::EXIT, false) << std::endl;
        std::cout
            << std::endl
            << "Current driver state: " << driverState << std::endl
            << std::endl;

        return;
    }


    void selectAction(action* curAction) {
        std::string strInput;
        std::getline(std::cin, strInput);
        std::cout << std::endl;

        if (strInput == "" || strInput.find_first_not_of("1234567890") != std::string::npos) {

            return;
        }

        int intInput = std::stoi(strInput);

        if (intInput >= action::MAX_ACTION) {

            return;
        }

        *curAction = static_cast<action>(intInput);

        return;
    }


    static std::string getActionString(action act, bool isSelected) {
        std::string strNum = std::to_string(act);
        std::string preSpace(4 - strNum.length(), ' ');
        std::string actionString = preSpace + strNum + "  " + actionStrings.at(act);

        if (isSelected) {
            actionString.replace(3 - strNum.length(), 1, "[");
            actionString.replace(4, 1, "]");
        }

        return actionString;
    }

}