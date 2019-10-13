# wreckfest-results-ocr

Extract data from screenshots of Wreckfest race results and dump those as plain text.

## Usage

Call the program with a Wreckfest screenshot picture either on the command line or by dropping the file over the executable in your file manager.
It will store a CSV file with the extracted data in the current directory. This can be opened in Excel or any other spreadsheet editor.

### Assign score based on position

TODO

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
