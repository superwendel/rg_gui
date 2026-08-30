@echo off
setlocal

rem Run every target from the repository root, even when this batch file is
rem called from another working directory. The wrapper guarantees that the
rem caller's working directory is restored on every exit path.
if defined _RG_GUI_BUILD_IN_ROOT goto build_main
set "_RG_GUI_BUILD_IN_ROOT=1"
pushd "%~dp0" >nul 2>nul
if errorlevel 1 (
	echo Could not enter the rg_gui repository directory: %~dp0
	exit /b 1
)
call "%~f0" %*
set "_RG_GUI_BUILD_EXIT=%errorlevel%"
popd
exit /b %_RG_GUI_BUILD_EXIT%

:build_main
set "RG_GUI_ROOT=%~dp0"
set "TARGET=%~1"
if not defined TARGET set "TARGET=test"
if not defined RG_CORE_DIR set "RG_CORE_DIR=%~dp0..\rg_core"
if not defined RG_TEXT_DIR set "RG_TEXT_DIR=%~dp0..\rg_text"

if /I "%TARGET%"=="clean" goto clean
if /I "%TARGET%"=="test" goto test
if /I "%TARGET%"=="test_ci" goto test_ci
if /I "%TARGET%"=="test_release" goto test_release
if /I "%TARGET%"=="test_assets" goto test_assets
if /I "%TARGET%"=="test_no_string_ids" goto test_no_string_ids
if /I "%TARGET%"=="test_gui" goto test_gui
if /I "%TARGET%"=="test_renderer" goto test_renderer
if /I "%TARGET%"=="test_gpu" goto test_gpu
if /I "%TARGET%"=="test_gpu_prepare" goto test_gpu_prepare
if /I "%TARGET%"=="test_gpu_device_build" goto test_gpu_device_build
if /I "%TARGET%"=="test_gpu_device" goto test_gpu_device
if /I "%TARGET%"=="shaders" goto shaders
if /I "%TARGET%"=="demo" goto demo
if /I "%TARGET%"=="demo_minimal" goto demo_minimal
if /I "%TARGET%"=="demo_full" goto demo_full
if /I "%TARGET%"=="demo_tearout" goto demo_tearout
if /I "%TARGET%"=="demo_smoke" goto demo_smoke
if /I "%TARGET%"=="run_demo" goto run_demo
if /I "%TARGET%"=="run_demo_minimal" goto run_demo_minimal
if /I "%TARGET%"=="run_demo_full" goto run_demo_full
if /I "%TARGET%"=="run_demo_tearout" goto run_demo_tearout

echo Unknown target: %TARGET%
exit /b 1

:ensure_compiler
where cl >nul 2>nul
if errorlevel 1 goto ensure_compiler_find
if not errorlevel 0 goto ensure_compiler_find
exit /b 0
:ensure_compiler_find
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo Could not find cl.exe or vswhere.exe.
	exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
	echo Could not find a Visual Studio C++ installation.
	exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:validate_dependencies
if not exist "%RG_CORE_DIR%\src\rg_defs.h" (
	echo rg_core not found. Set RG_CORE_DIR to the rg_core repository root.
	exit /b 1
)
if not exist "%RG_TEXT_DIR%\src\rg_text.h" (
	echo rg_text not found. Set RG_TEXT_DIR to the rg_text repository root.
	exit /b 1
)
exit /b 0

:find_sdl
if defined SDL3_INCLUDE_DIR if defined SDL3_LIB_DIR goto find_sdl_validate
if defined SDL3_DIR goto find_sdl_from_root
if defined VCPKG_INSTALLED_DIR if exist "%VCPKG_INSTALLED_DIR%\x64-windows\include\SDL3\SDL.h" set "SDL3_DIR=%VCPKG_INSTALLED_DIR%\x64-windows"
if not defined SDL3_DIR if exist "%RG_GUI_ROOT%vcpkg_installed\x64-windows\include\SDL3\SDL.h" set "SDL3_DIR=%RG_GUI_ROOT%vcpkg_installed\x64-windows"
if not defined SDL3_DIR if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\installed\x64-windows\include\SDL3\SDL.h" set "SDL3_DIR=%VCPKG_ROOT%\installed\x64-windows"

:find_sdl_from_root
if not defined SDL3_INCLUDE_DIR set "SDL3_INCLUDE_DIR=%SDL3_DIR%\include"
if not defined SDL3_LIB_DIR if exist "%SDL3_DIR%\lib\x64\SDL3.lib" set "SDL3_LIB_DIR=%SDL3_DIR%\lib\x64"
if not defined SDL3_LIB_DIR if exist "%SDL3_DIR%\lib\SDL3.lib" set "SDL3_LIB_DIR=%SDL3_DIR%\lib"
if not defined SDL3_BIN_DIR if exist "%SDL3_DIR%\bin" set "SDL3_BIN_DIR=%SDL3_DIR%\bin"
if not defined SDL3_BIN_DIR if exist "%SDL3_DIR%\lib\x64\SDL3.dll" set "SDL3_BIN_DIR=%SDL3_DIR%\lib\x64"
:find_sdl_validate
if not exist "%SDL3_INCLUDE_DIR%\SDL3\SDL.h" exit /b 1
if not exist "%SDL3_LIB_DIR%\SDL3.lib" exit /b 1
exit /b 0

:setup_host
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_dependencies
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set HOST_FLAGS=/nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I "%RG_CORE_DIR%\src" /I "%RG_TEXT_DIR%\src"
exit /b 0

:setup
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_dependencies
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :find_sdl
if errorlevel 1 (
	echo SDL3 not found. Set SDL3_DIR to the SDL3 development package root.
	exit /b 1
)
if not errorlevel 0 (
	echo SDL3 not found. Set SDL3_DIR to the SDL3 development package root.
	exit /b 1
)
set "PATH=%SDL3_BIN_DIR%;%SDL3_LIB_DIR%;%PATH%"
set GUI_FLAGS=/nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I "%RG_CORE_DIR%\src" /I "%RG_TEXT_DIR%\src" /I "%SDL3_INCLUDE_DIR%"
exit /b 0

:test
call "%~f0" test_no_string_ids
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gui
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_renderer
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui host-side and GPU-preparation tests passed. GPU device execution was not run; use test_release.
exit /b 0

:test_ci
call "%~f0" test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_assets
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu_device_build
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" demo
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui CI suite passed. GPU device execution was not run; use test_release.
exit /b 0

:test_release
call "%~f0" test_ci
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu_device
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :run_demo_smoke_built
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo All rg_gui release tests passed.
exit /b 0

:test_assets
call :setup_host
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building renderer-neutral demo asset validation tests...
cl %HOST_FLAGS% tests\test_assets.c /Fe:test_assets.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_assets.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_asset_hashes.ps1
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_no_string_ids
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building RG_GUI_NO_STRING_IDS configuration checks...
cl %GUI_FLAGS% tests\test_gui_no_string_ids.c /Fe:test_gui_no_string_ids.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_gui_no_string_ids.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
cl %GUI_FLAGS% /DRG_GUI_TEST_EXPECT_STRING_ID_FAILURE /c tests\test_gui_no_string_ids.c /Fo:test_gui_no_string_ids_negative.obj >nul 2>nul
if errorlevel 1 goto test_no_string_ids_expected_failure
echo RG_GUI_NO_STRING_IDS unexpectedly allowed a disabled string-ID API.
exit /b 1

:test_no_string_ids_expected_failure
echo RG_GUI_NO_STRING_IDS rejected disabled string-ID APIs as expected.
exit /b 0

:test_gui
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building rg_gui tests...
cl %GUI_FLAGS% tests\test_gui.c /Fe:test_gui.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_gui.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_renderer
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building rg_gui_renderer tests...
cl %GUI_FLAGS% tests\test_renderer.c /Fe:test_renderer.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_renderer.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu
:test_gpu_prepare
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building rg_gui_gpu host-side preparation checks; no GPU device will be used...
cl %GUI_FLAGS% tests\test_gui_gpu.c /Fe:test_gui_gpu.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_gui_gpu.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu_device
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rem Keep SDL3_BIN_DIR on this batch's PATH while the device test runs.
call :test_gpu_device_build
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_gui_gpu_device.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu_device_build
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Building rg_gui_gpu SDL_GPU device checks...
cl %GUI_FLAGS% tests\test_gui_gpu_device.c /Fe:test_gui_gpu_device.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:demo
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "DEMO_FLAGS=%GUI_FLAGS% /DNDEBUG"
call :build_demo_minimal
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :build_demo_full
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :build_demo_tearout
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo All rg_gui demos built successfully.
exit /b 0

:demo_minimal
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "DEMO_FLAGS=%GUI_FLAGS% /DNDEBUG"
call :build_demo_minimal
exit /b %errorlevel%

:demo_full
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "DEMO_FLAGS=%GUI_FLAGS% /DNDEBUG"
call :build_demo_full
exit /b %errorlevel%

:demo_tearout
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "DEMO_FLAGS=%GUI_FLAGS% /DNDEBUG"
call :build_demo_tearout
exit /b %errorlevel%

:demo_smoke
call "%~f0" demo
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :run_demo_smoke_built
exit /b %errorlevel%

:run_demo_smoke_built
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo Running local GPU smoke tests for all rg_gui demos...
rg_gui_demo_minimal.exe --smoke-test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_gui_demo_full.exe --smoke-test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_gui_demo_tearout.exe --smoke-test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo All rg_gui demo GPU smoke tests passed.
exit /b 0

:build_demo_minimal
echo Building minimal rg_gui demo...
cl %DEMO_FLAGS% examples\rg_gui_demo_minimal.c /Fe:rg_gui_demo_minimal.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui_demo_minimal.exe built successfully.
exit /b 0

:build_demo_full
echo Building full rg_gui showcase...
cl %DEMO_FLAGS% examples\rg_gui_demo_full.c /Fe:rg_gui_demo_full.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui_demo_full.exe built successfully.
exit /b 0

:build_demo_tearout
echo Building native tear-out rg_gui demo...
cl %DEMO_FLAGS% examples\rg_gui_demo_tearout.c /Fe:rg_gui_demo_tearout.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui_demo_tearout.exe built successfully.
exit /b 0

:run_demo
goto run_demo_full

:run_demo_minimal
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" demo_minimal
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_gui_demo_minimal.exe %RG_GUI_DEMO_ARGS%
exit /b %errorlevel%

:run_demo_full
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" demo_full
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_gui_demo_full.exe %RG_GUI_DEMO_ARGS%
exit /b %errorlevel%

:run_demo_tearout
call :setup
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" demo_tearout
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_gui_demo_tearout.exe %RG_GUI_DEMO_ARGS%
exit /b %errorlevel%

:find_shadercross
if defined SHADERCROSS_EXE goto find_shadercross_validate
if defined VCPKG_INSTALLED_DIR call :find_shadercross_in_prefix "%VCPKG_INSTALLED_DIR%\x64-windows"
if not defined SHADERCROSS_EXE call :find_shadercross_in_prefix "%RG_GUI_ROOT%vcpkg_installed\x64-windows"
if not defined SHADERCROSS_EXE if defined VCPKG_ROOT call :find_shadercross_in_prefix "%VCPKG_ROOT%\installed\x64-windows"
if not defined SHADERCROSS_EXE for %%i in (shadercross.exe) do set "SHADERCROSS_EXE=%%~$PATH:i"
if not defined SHADERCROSS_EXE if exist "C:\libs\SDL3_shadercross\bin\shadercross.exe" set "SHADERCROSS_EXE=C:\libs\SDL3_shadercross\bin\shadercross.exe"
if not defined SHADERCROSS_EXE call :find_unique_shadercross_fallback

:find_shadercross_validate
if not exist "%SHADERCROSS_EXE%" exit /b 1
exit /b 0

:find_shadercross_in_prefix
if exist "%~1\tools\sdl3-shadercross\shadercross.exe" set "SHADERCROSS_EXE=%~1\tools\sdl3-shadercross\shadercross.exe"
if not defined SHADERCROSS_EXE if exist "%~1\tools\sdl3_shadercross\shadercross.exe" set "SHADERCROSS_EXE=%~1\tools\sdl3_shadercross\shadercross.exe"
exit /b 0

:consider_shadercross_fallback
if not exist "%~1\bin\shadercross.exe" exit /b 0
set /a _RG_GUI_SHADERCROSS_FALLBACK_COUNT+=1
set "_RG_GUI_SHADERCROSS_FALLBACK=%~1\bin\shadercross.exe"
exit /b 0

:find_unique_shadercross_fallback
set "_RG_GUI_SHADERCROSS_FALLBACK_COUNT=0"
set "_RG_GUI_SHADERCROSS_FALLBACK="
for /d %%i in ("C:\libs\SDL3_shadercross-*") do call :consider_shadercross_fallback "%%~fi"
if "%_RG_GUI_SHADERCROSS_FALLBACK_COUNT%"=="1" set "SHADERCROSS_EXE=%_RG_GUI_SHADERCROSS_FALLBACK%"
if %_RG_GUI_SHADERCROSS_FALLBACK_COUNT% GTR 1 echo Multiple SDL_shadercross installations found under C:\libs; set SHADERCROSS_EXE explicitly.
exit /b 0

:shaders
call :find_shadercross
if errorlevel 1 (
	echo SDL_shadercross not found. Set SHADERCROSS_EXE or add shadercross.exe to PATH.
	exit /b 1
)
if not errorlevel 0 (
	echo SDL_shadercross not found. Set SHADERCROSS_EXE or add shadercross.exe to PATH.
	exit /b 1
)

if not exist "shaders\Compiled\DXIL" mkdir "shaders\Compiled\DXIL"
if not exist "shaders\Compiled\SPIRV" mkdir "shaders\Compiled\SPIRV"
if not exist "shaders\Compiled\MSL" mkdir "shaders\Compiled\MSL"

call :compile_shader rg_gui_text.vert vertex
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :compile_shader rg_gui_text.frag fragment
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :compile_shader rg_gui_expand.comp compute
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :compile_shader rg_gui_geometry.vert vertex
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :compile_shader rg_gui_solid.frag fragment
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :compile_shader rg_gui_image.frag fragment
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_gui shaders compiled successfully.
exit /b 0

:compile_shader
"%SHADERCROSS_EXE%" "shaders\%1.hlsl" -s HLSL -d DXIL -t %2 -e main -o "shaders\Compiled\DXIL\%1.dxil"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%SHADERCROSS_EXE%" "shaders\%1.hlsl" -s HLSL -d SPIRV -t %2 -e main -o "shaders\Compiled\SPIRV\%1.spv"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%SHADERCROSS_EXE%" "shaders\%1.hlsl" -s HLSL -d MSL -t %2 -e main -o "shaders\Compiled\MSL\%1.msl"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:clean
rem Every target below is rooted at this batch file's verified repository path.
del /q "%RG_GUI_ROOT%test_assets.exe" "%RG_GUI_ROOT%test_gui_no_string_ids.exe" "%RG_GUI_ROOT%test_gui.exe" "%RG_GUI_ROOT%test_renderer.exe" "%RG_GUI_ROOT%test_gui_gpu.exe" "%RG_GUI_ROOT%test_gui_gpu_device.exe" 2>nul
del /q "%RG_GUI_ROOT%rg_gui_demo_minimal.exe" "%RG_GUI_ROOT%rg_gui_demo_full.exe" "%RG_GUI_ROOT%rg_gui_demo_tearout.exe" 2>nul
del /q "%RG_GUI_ROOT%test_assets.obj" "%RG_GUI_ROOT%test_gui_no_string_ids.obj" "%RG_GUI_ROOT%test_gui_no_string_ids_negative.obj" "%RG_GUI_ROOT%test_gui.obj" "%RG_GUI_ROOT%test_renderer.obj" "%RG_GUI_ROOT%test_gui_gpu.obj" "%RG_GUI_ROOT%test_gui_gpu_device.obj" 2>nul
del /q "%RG_GUI_ROOT%rg_gui_demo_minimal.obj" "%RG_GUI_ROOT%rg_gui_demo_full.obj" "%RG_GUI_ROOT%rg_gui_demo_tearout.obj" 2>nul
del /q "%RG_GUI_ROOT%bake_demo_font.obj" "%RG_GUI_ROOT%bake_ui_font.obj" 2>nul
if exist "%RG_GUI_ROOT%shaders\Compiled" rmdir /s /q "%RG_GUI_ROOT%shaders\Compiled"
for %%f in (test_assets.exe test_gui_no_string_ids.exe test_gui.exe test_renderer.exe test_gui_gpu.exe test_gui_gpu_device.exe rg_gui_demo_minimal.exe rg_gui_demo_full.exe rg_gui_demo_tearout.exe test_assets.obj test_gui_no_string_ids.obj test_gui_no_string_ids_negative.obj test_gui.obj test_renderer.obj test_gui_gpu.obj test_gui_gpu_device.obj rg_gui_demo_minimal.obj rg_gui_demo_full.obj rg_gui_demo_tearout.obj bake_demo_font.obj bake_ui_font.obj) do if exist "%RG_GUI_ROOT%%%f" (
	echo Failed to remove build artifact: %%f
	exit /b 1
)
if exist "%RG_GUI_ROOT%shaders\Compiled" (
	echo Failed to remove compiled shader directory: %RG_GUI_ROOT%shaders\Compiled
	exit /b 1
)
exit /b 0
