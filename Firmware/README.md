This folder contains the source code files for the firmware that runs on the embedded microcontroller.

## Overview

The firmware runs on an embedded RISC-V microcontroller, a [WCH CH32X035](https://www.wch-ic.com/products/CH32X035.html), which serves as a USB host and handles enumeration of the HID USB pointing device and reception of its regular reports. These reports are translated into the appropriate serial protocol format and sent via RS-232 serial UART to the host system. When the host system probes the serial port (by toggling the RTS line), the firmware also responds with identifying information.

The firmware code is written entirely in C.

## Updating the Firmware

The firmware of the on-board microcontroller can be upgraded via the USB port. You can find the latest firmware in the 'Releases' section of the GitHub repository.

### Pre-requisites

* Type-A male to Type-A male USB cable.
* A Windows computer with a USB port and [WCH ISP Tool software](https://www.wch-ic.com/downloads/WCHISPTool_Setup_exe.html) downloaded and installed.

### Procedure

1. Unplug cables from **all** connectors on the board – USB, serial, and power.
2. Set the jumper labelled `USB-PWR` to position 2-3.
3. Connect one end of A-to-A USB cable to the computer being used to run the update software.
4. Start the WCH ISP Tool software.
5. While holding down the `BOOT` button, plug other end of the USB cable into the board's USB connector.
6. After a couple of seconds, release the `BOOT` button.
7. The microcontroller should automatically be detected. The *Chip Option* panel should show series `CH32X03x`, model `CH32X035G8R6`, and port `USB`.
8. In the *Download File* panel, click the folder icon in the first row of the table (*Object File1*) and select the `.hex` firmware file. Ensure the checkbox in the right-hand column is ticked, and that all other rows are blank.
9. Click the *Download* button at the bottom of the window.
10. Wait for the firmware download process to occur. It should go through three stages: erasing, programming, and verifying, which should all report as completing successfully.
11. Unplug the USB cable.
12. Return `USB-PWR` jumper to position 1-2.
13. Reconnect USB mouse, serial, and power cables as they were before.

## Building

A Windows build environment with installations of the [xPack GCC RISC-V compiler](https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/) and [xPack Windows Build Tools](https://xpack-dev-tools.github.io/windows-build-tools-xpack/) is assumed. It may be possible to build on other platforms with the appropriate RISC-V cross-compiler (riscv-none-elf-gcc) and tools (make, objcopy, etc.) but this has not been provided for or tested.

### Compiling

1. Run the `setenv-prompt.bat` batch file. This will open a command prompt with the appropriate paths set for xPack tools.
2. From the command prompt, run `make` to compile.
3. Firmware files in both ELF (`.elf`) and Intel Hex format (`.hex`) will be placed in a `bin` sub-folder.

> [!NOTE]
> You may need to edit the `setenv-prompt.bat` batch file and alter the exact paths due to package release or version number differences.

To remove all build artifacts (binaries, object files, etc.) in the `bin` and `obj` folders, run `make clean`.

### Programming

The easiest procedure for programming the firmware on a newly-built board is to follow the procedure for USB firmware updates above.

Alternatively, if you have a copy of WCH's fork of OpenOCD installed (e.g. from MounRiver Studio) and in path, plus a WCH LinkE programming adapter connected to the `DEBUG` header, you can run `make flash` to program.

### Debugging

The firmware can optionally be configured to output various informational and diagnostic messages to a dedicated UART pin, labelled `TX`, present on the board's `DEBUG` header.

When compiling, give an argument of `DEBUG_VERBOSITY=<value>` to the `make` command, where 'value' is one of the following: `ERROR`, `WARN`, `INFO`, `TRACE`. The least verbose is `ERROR` and `TRACE` the most. To turn off all debug messages entirely, specify the value `OFF`.

The debug UART operates at a baud rate of 256 kbit, with 8 data bits, 1 stop bit, no parity.
