@echo off
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set GCC=gcc.exe
set CFLAGS=-Wall -O2 -Iinclude -IC:/msys64/ucrt64/include -DSQLITE_THREADSAFE=1 -D_WIN32_WINNT=0x0601

if not exist obj mkdir obj
if not exist db mkdir db
if not exist logs mkdir logs

echo === Building FoodRush Backend ===

%GCC% %CFLAGS% -c src/threadpool.c -o obj/threadpool.o 1>errors.txt 2>&1
set R1=%ERRORLEVEL%
if %R1% NEQ 0 goto fail_tp
echo [OK] threadpool.c

%GCC% %CFLAGS% -c src/database.c -o obj/database.o 1>errors.txt 2>&1
set R2=%ERRORLEVEL%
if %R2% NEQ 0 goto fail_db
echo [OK] database.c

%GCC% %CFLAGS% -c src/json_utils.c -o obj/json_utils.o 1>errors.txt 2>&1
set R3=%ERRORLEVEL%
if %R3% NEQ 0 goto fail_ju
echo [OK] json_utils.c

%GCC% %CFLAGS% -c src/router.c -o obj/router.o 1>errors.txt 2>&1
set R4=%ERRORLEVEL%
if %R4% NEQ 0 goto fail_rt
echo [OK] router.c

%GCC% %CFLAGS% -c src/handlers.c -o obj/handlers.o 1>errors.txt 2>&1
set R5=%ERRORLEVEL%
if %R5% NEQ 0 goto fail_hn
echo [OK] handlers.c

%GCC% %CFLAGS% -c src/main.c -o obj/main.o 1>errors.txt 2>&1
set R6=%ERRORLEVEL%
if %R6% NEQ 0 goto fail_mn
echo [OK] main.c

echo Linking...
%GCC% -o foodrush.exe obj/threadpool.o obj/database.o obj/json_utils.o obj/router.o obj/handlers.o obj/main.o -LC:/msys64/ucrt64/lib -lsqlite3 -lpthread -lws2_32 -static-libgcc 1>errors.txt 2>&1
set RL=%ERRORLEVEL%
if %RL% NEQ 0 goto fail_lk

echo.
echo ============================================
echo   BUILD SUCCESS: foodrush.exe
echo   Run: foodrush.exe
echo   Open: http://localhost:8080
echo ============================================
del errors.txt 2>nul
goto end

:fail_tp
echo FAIL threadpool.c - errors:
type errors.txt
goto end

:fail_db
echo FAIL database.c - errors:
type errors.txt
goto end

:fail_ju
echo FAIL json_utils.c - errors:
type errors.txt
goto end

:fail_rt
echo FAIL router.c - errors:
type errors.txt
goto end

:fail_hn
echo FAIL handlers.c - errors:
type errors.txt
goto end

:fail_mn
echo FAIL main.c - errors:
type errors.txt
goto end

:fail_lk
echo FAIL link - errors:
type errors.txt
goto end

:end
echo Build script finished.
