@echo off
call "%~dp0run_all_tests.bat" session_recording
exit /b %errorlevel%
