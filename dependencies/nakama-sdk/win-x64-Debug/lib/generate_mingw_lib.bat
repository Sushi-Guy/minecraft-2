@echo off
echo Generating .def file from nakama-sdk.dll...
gendef nakama-sdk.dll
echo Generating MinGW-compatible .a library...
dlltool -d nakama-sdk.def -l libnakama-sdk.a -D nakama-sdk.dll
echo Done! libnakama-sdk.a has been created.
pause
