REM Handling weird issue with BDS2006 and Windows 7

del %temp%\EditorLineEnds*.ttr
set dt=%date%_%time:~3,2%-%time:~6,2%
rename "%temp%\EditorLineEnds.ttr" "EditorLineEnds_%dt%.ttr"
start "" "C:\Program Files\Borland\BDS\4.0\Bin\bds.exe"