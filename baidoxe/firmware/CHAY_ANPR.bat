@echo off
chcp 65001 >nul
title KHOI DONG AI CAMERA NHAN DIEN BIEN SO - BAI DO XE ESP32
echo =====================================================================
echo    HE THONG AI CAMERA NHAN DIEN BIEN SO (YOLOv8 + EasyOCR)
echo    DONG BO VOI ESP32 (BAI DO XE THONG MINH 2 TRUC)
echo =====================================================================
echo.

cd /d "%~dp0"

echo [1/3] Kiem tra moi truong Python...
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo [LOI] Khong tim thay Python! Vui long cai dat Python 3.10 hoac 3.11 va tich chon "Add Python to PATH".
    pause
    exit /b
)

echo [2/3] Bo qua buoc kiem tra thu vien (da cai san)...
echo [3/3] Dang khoi chay chuong trinh Camera ANPR...
echo.
echo * PHIM NONG TRONG CUA SO CAMERA:
echo   - [T] hoac [SPACE] : Bat/Tat che do TEST doc lap (nhan dien khong can ESP32)
echo   - [C]              : Chuyen doi Webcam (Webcam 0 hoac Webcam 1)
echo   - [Q]              : Thoat chuong trinh
echo =====================================================================
python anpr_bai_do_xe_final.py

pause

