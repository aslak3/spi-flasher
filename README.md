# Pi Pico based SPI flash and iCE40 programmer

This repo holds the code for the two halves of a solution for programming
SPI flashes via a Pi Pico:

* The firmware for the Pi Pico, written in C
* A client application written in Go

The following connections are required:

| GPIO | Function |
|------|----------|
| 16   | MISO     |
| 19   | MOSO     |
| 18   | SCK      |
| 17   | CS       |
| 20   | CRESET   |
| 21   | CDONE    |

The last two are only required for programing an iCE40.

Build the Go tool following whatever method suits you, but `go build .` in
the tools/spi-flasher-client is a reasonable approach.

The tool takes four options:

--filename : the filename of the file to write, or read

--port : the serial port, defaulting to /dev/ttyACM0

--write-flash : used for writing the file to the SPI flash

--read-flash : used for reading the current complete content out of the SPI flash and writing it to the specified file

--write-fpga : used for writing the file to the iCE40 FPGA

This software is described in more detail on my blog, at:

https://www.aslak.net/index.php/2024/04/11/an-spi-flash-and-ice40up-fpga-programmer/
