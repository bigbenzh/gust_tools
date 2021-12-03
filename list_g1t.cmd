@echo off
setlocal EnableDelayedExpansion
set EXT=g1t
set TARGET=E:\SteamLibrary\steamapps\common\BLUE REFLECTION Second Light

call build.cmd %EXT%
if %ERRORLEVEL% neq 0 goto err

for /R "%TARGET%" %%f in (*.%EXT%) do (
  gust_%EXT%.exe -l "%%f"
)