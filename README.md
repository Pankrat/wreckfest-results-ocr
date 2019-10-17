# wreckfest-results-ocr

Extract data from screenshots of Wreckfest race results and dump those as plain text.

## Installation

Unzip the executable and the data directory in any place, e.g. the Wreckfest
screenshot directory for easy access.

## Usage

Call the program with a Wreckfest screenshot picture either on the command line
or by dropping the file over the executable in your file manager.
It will store a CSV file with the extracted data in the current directory. This
can be opened in Excel or any other spreadsheet editor.

### Notes

Screenshots should be made in fullscreen mode after the race has ended but
before players can return to the lobby. Screenshots can be made from a local
game or from a Youtube video or Twitch stream if the quality is good and the
result section is not obstructed by any banners or overlays.

### Assign score based on position

To assign points based on position, add a file `points.txt` with the following format:

```
1 20
2 15
...
DNF 0
```

This assigns 20 points to the player on the first position and 15 to the
second. 

### Driver names & teams

To correct the spelling of driver names and assign drivers to teams for
aggregated results, you can add a file `drivers.txt` to the directory of the
executable. The format is the following with one driver per line:

```
TeamName,DriverName
```

## Build instructions

This application requires tesseract (version 4 or higher) to be installed. 

### Linux

```
$ sudo apt-get install libtesseract-dev
$ make
```

### Windows

* Install vcpkg: https://github.com/microsoft/vcpkg
* Install tesseract via vcpkg: `.\vcpkg install tesseract:x64-windows-static`
* If it complains about a missing English language pack, install it via the Visual Studio Installer
* Configure VS solution to use statically linked libraries: https://devblogs.microsoft.com/cppblog/vcpkg-updates-static-linking-is-now-available/
* Select Console application in Project properties/Linker/System
* Select "Multithreaded (/MT)" in Project properties/C/C++/Code generation/Runtime environment

## Known issues

* Highlighted rows confuse the text extraction and might be missing from the output
* Tested with 1920x1080 and 3440x1440. Other resolutions or aspect ratios might not work yet.
