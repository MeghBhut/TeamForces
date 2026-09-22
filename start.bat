@echo off
REM Double-click this to run TeamForces on port 8090 (keeps 8080 free for
REM other apps like Label Studio). Close this window to stop the server.
cd /d "%~dp0"
echo Starting TeamForces on http://0.0.0.0:8090
echo Reach it from your tailnet at  http://<this-PC's-tailscale-ip>:8090
echo Close this window to stop.
teamforces.exe --port 8090
pause
