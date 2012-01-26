@echo off
if not exist .svn goto Unklar
if exist .svn-OpenMCP goto OpenMCP_exists
if exist .svn-Fred goto Fred_exists
goto Unklar

:OpenMCP_exists
if exist .svn-Fred goto Unklar
echo Schalte um auf OpenMCP
ren .svn .svn-Fred
ren .svn-OpenMCP .svn
echo Fertig!
goto Ende

:Fred_exists
if exist .svn-OpenMCP goto Unklar
echo Schalte um auf Fred
ren .svn .svn-OpenMCP
ren .svn-Fred .svn
echo Fertig!
goto Ende

:Unklar
echo Aktuelle SVN-Beziehung unklar, Verzeichnisse .svn* prüfen!

:Ende
pause
exit
