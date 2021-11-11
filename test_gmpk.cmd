@rem This test script performs unpack and repack of GMPK
@rem archives and validates that the data matches.
@rem The GMPK test files can be found in the PC version of
@rem Fatal Frame - Maiden of Black Water
@rem
@echo off
setlocal EnableDelayedExpansion
set EXT=gmpk
set TST=.test
call build.cmd cmp
call build.cmd %EXT%
if %ERRORLEVEL% neq 0 goto err

set list=^
  BOCHI_00^
  G_HAK_A^
  G_KAG_D^
  H_KYO_A^
  H_MIU_A^
  H_MIU_S^
  R_PHO_A^
  H_YRI_A^
  H_YRI_V

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
