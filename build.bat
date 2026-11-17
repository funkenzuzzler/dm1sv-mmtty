set PATH=%PATH%;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.12.0\ming64\bin
del build
cmake -B build -G "Ninja" -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\mingw_64
cmake --build build
windeployqt build\mmtty.exe