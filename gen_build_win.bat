@echo off
if not exist build (
    mkdir build
)
cd build
cmake ..\ -G "Visual Studio 16 2019" -A x64 -DTarget=Windows -DArch=x64
cd ..\
pause