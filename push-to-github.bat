@echo off
rem Klik dua kali file ini untuk push ke GitHub.
rem Menjalankan push-to-github.ps1 dengan execution policy di-bypass.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0push-to-github.ps1"
