Bootloader Update System
========================

For chipKIT boards
------------------

This sketch will allow you to update the bootloader on
your chipKIT board. It comes bundled with a number of
common board's bootloaders, but if your board doesn't
have the bootloader bundled you can upload the .HEX file
for the bootloader to the board through serial.

This is a fully command-line driven system where you
type in commands to make it do thing.  

It also has the ability to change the USERID in the
DEVCFG3 config register, which is available for
use as a USB serial number on some boards.

Requirements
------------

* CLI library: https://github.com/MajenkoLibraries/CLI

Caveats
-------

**PIC32MX1xx/2xx**

Clock switching is required to be enabled in the existing
bootloader. 

There are some issues programming 
the top page of boot flash since that is where the config bits
are. Erasing that page can crash the whole chip.  To counter 
this the clock source is switched prior to writing the last
page of flash to the FRC+FRCDIV clock source. Because of 
this the USB communications will be disabled at this point
and cannot be re-enabled. It is impossible to switch back
to the normal clock source after writing the last page.

If clock switching is disabled the CPU will stall
when the config bits are erased and writing the new
config bits will not be possible, leaving the chip
in an unconfigured state. The bootloader will only be
part programmed and unable to execute. The only recovery
method is to write the bootloader using a hardware
programmer such as a PICkit3™.

**USB**

It is recommended that you use an external USB to TTL Serial
adapter and communicate using the normal UART pins of your
board instead of the USB port for pure-USB devices (that is
devices that lack an FT232 chip as a USB interface).

Usage
-----

The UART is configured for 9600 8-N-1 operation. 

    Commands:
      help              This screen.
      load <source>     Load bootloader from <source>
           internal     Load internally bundled bootloader
           ascii        Receive a plain text HEX file as ASCII
      info              Display current state information
      burn              Burn the loaded bootloader
      dump              Dump the bootloader to the screen
      userid <uid>      Set the board's UUID
      reboot            Reboot the board

Disclaimer
----------

This software comes with no warranty whatsoever. Use of it
is entirely at the user's own risk. Majenko Technologies,
chipKIT, nor any of its associates, members or partners,
can be held responsible for problems arising from the use
of this software.
