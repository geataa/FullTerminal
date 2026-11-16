@echo off
call "%~dp0run_all_tests.bat" osc_semantics
exit /b %errorlevel%
