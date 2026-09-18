@echo off
cd /d "D:\ChatBot Project\ChatBot\build\bin\Release"
start "" ChatBot.exe
timeout /t 2 >nul
start "" C:\Users\z'q\AppData\Local\Microsoft\WindowsApps\ngrok.exe http 8080