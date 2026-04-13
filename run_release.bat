@echo off
chcp 65001 >nul
cd /d "%~dp0\build_new\bin\Release"
start VideoPlay.exe
