' Double-click this to start TeamForces + Label Studio in the background and
' lock the screen. No console window ever appears.
' If something fails to start, a dialog appears and the screen is NOT locked.
Option Explicit

Dim fso, sh, here, cmd
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh  = CreateObject("WScript.Shell")

here = fso.GetParentFolderName(WScript.ScriptFullName)

cmd = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File """ & _
      fso.BuildPath(here, "launch.ps1") & """"

' 0 = hidden window, False = do not wait for it to finish
sh.Run cmd, 0, False
