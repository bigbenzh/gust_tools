@echo off
setlocal EnableDelayedExpansion
set EXT=gmpk
set TARGET=E:\SteamLibrary\steamapps\common\FATAL FRAME MOBW

call build.cmd %EXT%
if %ERRORLEVEL% neq 0 goto err

for /R "%TARGET%" %%f in (*.%EXT%) do (
  gust_gmpk.exe -l "%%f"
)