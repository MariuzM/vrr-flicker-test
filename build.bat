@echo off
if not exist build mkdir build
cd build
cmake ..
cmake --build . --config Release
echo.
echo Build complete. Run: build\Release\vrr_flicker_test.exe
