@echo off
call "%~dp0run_all_tests.bat" ssh_loading_fleet
exit /b %errorlevel%
