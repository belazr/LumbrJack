# LumbrJack
LumbrJack is a very basic kernel mode logger for 64 bit Windows.
It currently logs all keystrokes of the keyboard and all left/right mouse clicks with relative coordinates of the cursor to text files.

It consists of a kernel mode filter driver (using WDM) and a user mode client application to control the driver.

This project was created to learn and help others to learn windows driver development.
It is higly experimental and only intended for learing purposes. Use at your own risk!

## Requirements
- Windows 10/11 64bit
- Visual Studio
- C99 compliant compiler
- C++ 14 compliant compiler
- Windows SDK
- Windows Driver Kit

Build tested with:
- Windows 11 64bit
- Visual Studio 17
- MSVC v143
- Windows 11 SDK (10.0.22621)
- Windows Driver Kit (10.0.22621)

Usage tested with:
- Windows 11 64bit

## Build
Open the solution file (LumbrJack.sln) with Visual Studio and run the desired builds from there.
Client: By default an executable with static runtime library linkage (/MT and /MTd) is built, so it is completely protable.

## Usage
It is strongly advised to only use LumbrJack within a virtual environment.

Open a cmd prompt as administator and run:
```
bcdedit /set testsigning on
```
to allow runing unsigned drivers on your system. Reboot the machine.

Then run the client application as an administrator with the location of the driver executable ("LumberJackDriver.sys") as a parameter.
For example from an admin cmd prompt:
```
C:\LumbrJackClient.exe C:\LumbrJackDriver.sys
```

Now select an option from the menu by entering the item number and press enter.
1) First the driver needs to be installed as a service, so select **1**.
2) Then you can start the driver by selecting **2**.
3) To start logging select **6**. Now all keystrokes/mouseclicks get logged to the respective log files: "C:\kbd.log" for keyboard and "C:\mou.log" for mouse logging. **If these files already exist they will be overwritten.**
4) To stop logging select **7**. Each log file logs one more action until it is released. Press a key on the keyboard or a mouse button if log files are still locked. Now you can open the log files and inspect the output.
5) To stop the driver select **3**. If the driver is still logging, up to two keystrokes/mouseclicks are needed to close the log files and unload the driver completely.
6) To uninstall the driver select **4**.

## Known Issues
- Scan code to ascii lookup array is only partially correct and only compatible with german keyboard layouts

## TODOs
- Fix scan code to ascii lookup array
- Add logging for file operations