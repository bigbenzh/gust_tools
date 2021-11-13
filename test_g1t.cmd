@rem This test script performs unpack and repack of G1T
@rem textures and validates that the data matches.
@rem The texture test files can be downloaded from:
@rem https://vitasmith.rpc1.org/gust_tools/test_g1t.7z
@rem
@echo off
setlocal EnableDelayedExpansion
set EXT=g1t
set TST=.test
call build.cmd cmp
call build.cmd %EXT%
if %ERRORLEVEL% neq 0 goto err

set list=^
  type_01_sw^
  type_08_ps3^
  type_09_ps4^
  type_10_psv^
  type_10_psv_2^
  type_12_psv^
  type_12_psv_2^
  type_21_sw^
  type_3c_3ds^
  type_3d_3ds^
  type_45_3ds^
  type_59_win^
  type_59_win_2^
  type_5b_win^
  type_5b_win_2^
  type_5b_win_3^
  type_5c_win^
  type_5f_win^
  type_5f_win_2^
  type_60_ps4^
  type_62_ps4^
  type_62_ps4_2

for %%a in (%list%) do (
  if exist %%a.%EXT%.bak move /y %%a.%EXT%.bak %%a.%EXT% >NUL 2>&1
)

for %%a in (%list%) do (
  echo | set /p PrintName=* %%a.%EXT%... 
  if exist %%a.%EXT% (
    gust_%EXT%.exe -y %%a.%EXT% >%TST% 2>&1
    if !ERRORLEVEL! neq 0 goto err
    gust_%EXT%.exe -y %%a >%TST% 2>&1
    if !ERRORLEVEL! neq 0 goto err
    gust_cmp.exe %%a.%EXT% %%a.%EXT%.bak >%TST% 2>&1
    if !ERRORLEVEL! neq 0 goto err
    echo 	[PASS]
  ) else (
    echo 	[SKIP]
  )
)

echo [ALL TESTS PASSED]
goto out

:err
echo 	[FAIL]
echo.
echo ----------------------- FAILURE DATA -----------------------
type %TST%
echo ------------------------------------------------------------

:out
for %%a in (%list%) do (
  if exist %%a.%EXT%.bak move /y %%a.%EXT%.bak %%a.%EXT% >NUL 2>&1
)
del /q %TST% >NUL 2>&1
