' Double-click this to stop the background services.
' Shows a window with the result so you can see what was stopped.
Option Explicit

Dim fso, sh, here, cmd
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh  = CreateObject("WScript.Shell")

here = fso.GetParentFolderName(WScript.ScriptFullName)

cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -File """ & _
      fso.BuildPath(here, "stop.ps1") & """"

' 1 = normal visible window, so the output is readable
sh.Run cmd, 1, False
