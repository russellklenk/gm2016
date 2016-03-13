@ECHO OFF

SET VSVERSION_2013=12.0
SET VSVERSION_2015=14.0
SET VSVERSION=%VSVERSION_2015%

IF NOT DEFINED DevEnvDir (
    CALL "C:\Program Files (x86)\Microsoft Visual Studio %VSVERSION%\VC\vcvarsall.bat" x86_amd64
)

SET OUTPUTDIR="%~dp0build"
SET INCLUDES=-I..\include -I..\src -I%VULKAN_SDK%\Include
SET DEFINES=/D _WIN32_WINNT=0x06000 /D UNICODE /D _UNICODE /D BUILD_STATIC /D VK_USE_PLATFORM_WIN32_KHR /D VK_PROTOTYPES
SET CPPFLAGS=%INCLUDES% /FC /nologo /W4 /WX /wd4505 /Zi /EHsc /Ox
SET LIBRARIES=User32.lib Gdi32.lib Shell32.lib Advapi32.lib winmm.lib tdh.lib %VULKAN_SDK%\Source\lib\vulkan-1.lib
SET LNKFLAGS=%LIBRARIES%

IF NOT EXIST %OUTPUTDIR% mkdir %OUTPUTDIR%

xcopy %VULKAN_SDK%\Source\lib\vulkan-1.dll %OUTPUTDIR% /Y
xcopy %VULKAN_SDK%\Source\lib\vulkan-1.pdb %OUTPUTDIR% /Y

PUSHD %OUTPUTDIR%
cl %CPPFLAGS% ..\src\win32_client.cc %DEFINES% %LNKFLAGS% /Fegclient.exe
POPD

