# Installation instructions

## Dependencies
- Visual Studio >= 2019

## Intallation steps

Run the installation script install.ps1

The script will allow you to select the subset of libraries you want to download, compile, and install on your system. All the libraries will be installed in the download folder and will not be available globally to the OS, as this allows to use only local paths for building and executing the benchmarks.
The following libraries will have to be installed manually by the user since they rely on specific hardware configurations:
-  nvjpeg
-  nvjpeg2k

Naturally, the subset of selected libraries will enable the corresponding benchmarks.
