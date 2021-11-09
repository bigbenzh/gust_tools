@rem This test script performs unpack and repack of GMPK
@rem archives and validates that the data matches.
@rem The GMPK test files can be found in the PC version of
@rem Fatal Frame - Maiden of Black Water
@rem
@echo off
setlocal EnableDelayedExpansion
set EXT=gmpk
call build.cmd %EXT%
if %ERRORLEVEL% neq 0 goto err

set list=^
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
    gust_%EXT%.exe -y %%a.%EXT% >NUL 2>&1
    if !ERRORLEVEL! neq 0 goto err
    gust_%EXT%.exe %%a >NUL 2>&1
    if !ERRORLEVEL! neq 0 goto err
    fc.exe /b %%a.%EXT% %%a.%EXT%.bak >NUL 2>&1
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

:out
for %%a in (%list%) do (
  if exist %%a.%EXT%.bak move /y %%a.%EXT%.bak %%a.%EXT% >NUL 2>&1
)
