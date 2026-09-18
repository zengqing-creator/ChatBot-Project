@echo off
cd /d "D:\ChatAI Project\ChatBot\build\bin"
start "" ChatBot.exe
timeout /t 2 >nul
start "" C:\Users\z'q\AppData\Local\Microsoft\WindowsApps\ngrok.exe http 8080