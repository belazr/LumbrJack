#pragma once
#include <Windows.h>

// Handles console output and user input.
namespace io {

	// Options for user selection.
	enum action { EXIT = 0, INSTALL, START, STOP, UNINSTALL, LOG_STATE, LOG_START, LOG_STOP, MAX_ACTION };

	// Prints the menu.
	// 
	// Parameters:
	// 
	// [in] curAction:
	// Action currently selected. Number is printed in brackets: " [2] label".
	//
	// [in] driverState:
	// State of the driver.
	void printMenu(action curAction, DWORD driverState);

	// Lets the user select the action to be executed.
	// 
	// Parameters:
	// 
	// [in/out] pAction:
	// Action currently selected. Only overwritten for valid user input. For invalid input it keeps its value.
	void selectAction(action* pAction);
}

