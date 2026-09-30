@echo off
echo FullTerminal - Temizlik yapiliyor...
del /q *.obj 2>nul
del /q *.res 2>nul
del /q *.ilk 2>nul
del /q *.pdb 2>nul
if exist build rmdir /s /q build
del /q bin\*.obj 2>nul
del /q bin\*.res 2>nul
del /q bin\*.ilk 2>nul
del /q bin\*.pdb 2>nul
del /q tests\*.obj 2>nul
del /q tests\*.exe 2>nul
del /q tests\*.ilk 2>nul
del /q tests\*.pdb 2>nul
echo Temizlik tamamlandi.
