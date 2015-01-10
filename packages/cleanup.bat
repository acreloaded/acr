@echo off
echo ------------------
echo AssaultCube Package Cleanup v0.1
echo ------------------
echo.
echo This script can be used to remove unnecessary files from the AssaultCube
echo package directory.
echo.
echo For copyright reasons, AssaultCube Reloaded (ACR) is required to distribute the
echo entire AssaultCube (AC) package. Because ACR overrides some files in the AC
echo package, those files become unnecessary and may be removed.

choice /M "Delete unnecessary files"
IF ERRORLEVEL 2 GOTO :EOF

call :rmf audio\misc\headshot.ogg
call :rmf audio\weapon\auto.ogg
call :rmf audio\weapon\shotgun.ogg
call :rmf audio\weapon\shotgun_reload.ogg
call :rmf audio\weapon\sniper.ogg
call :rmf audio\weapon\sniper_reload.ogg
call :rmf audio\weapon\sub.ogg
call :rmf audio\weapon\sub_reload.ogg
call :rmf audio\weapon\usp.ogg
call :rmf misc\base.png
call :rmf misc\blood.png
call :rmf misc\compass-base.png
call :rmf misc\compass-rose.png
call :rmf misc\ctficons.png
call :rmf misc\damage.png
call :rmf misc\explosion.png
call :rmf misc\huddigits.png
call :rmf misc\items.png
call :rmf misc\muzzleflash.jpg
call :rmf misc\scope.png
call :rmf misc\smoke.png
call :rmf misc\startscreen.png
call :rmf misc\teamicons.png
call :rmf misc\voteicons.png
call :rmf models\playermodels\CLA\01.jpg
call :rmf models\playermodels\CLA\01_redvest.jpg
call :rmf models\playermodels\CLA\02.jpg
call :rmf models\playermodels\CLA\02_redvest.jpg
call :rmf models\playermodels\CLA\03.jpg
call :rmf models\playermodels\CLA\03_redvest.jpg
call :rmf models\playermodels\CLA\04.jpg
call :rmf models\playermodels\CLA\04_redvest.jpg
call :rmf models\playermodels\CLA\red.jpg
call :rmf models\playermodels\RVSF\01.jpg
call :rmf models\playermodels\RVSF\01_bluevest.jpg
call :rmf models\playermodels\RVSF\02.jpg
call :rmf models\playermodels\RVSF\02_bluevest.jpg
call :rmf models\playermodels\RVSF\03.jpg
call :rmf models\playermodels\RVSF\03_bluevest.jpg
call :rmf models\playermodels\RVSF\04.jpg
call :rmf models\playermodels\RVSF\04_bluevest.jpg
call :rmf models\playermodels\RVSF\05.jpg
call :rmf models\playermodels\RVSF\05_bluevest.jpg
call :rmf models\playermodels\RVSF\06.jpg
call :rmf models\playermodels\RVSF\06_bluevest.jpg
call :rmf models\playermodels\RVSF\blue.jpg
call :rmf models\playermodels\skin.jpg
REM models\weapons\assault\menu is unnecessary?
call :rmf models\weapons\assault\md3.cfg
call :rmf models\weapons\assault\skin_weapon.jpg
call :rmf models\weapons\assault\tris_high.md3
call :rmd models\weapons\carbine
call :rmf models\weapons\knife\md3.cfg
call :rmf models\weapons\pistol\md3.cfg
call :rmf models\weapons\pistol\skin_weapon.jpg
call :rmf models\weapons\shotgun\md3.cfg
call :rmf models\weapons\sniper\md3.cfg
call :rmf models\weapons\subgun\md3.cfg
REM rd models\weapons\knife
REM rd models\weapons\pistol
REM rd models\weapons\shotgun
REM rd models\weapons\sniper
REM rd models\weapons\subgun

echo Done!
GOTO :EOF

:rmf
IF EXIST %1 del /F %1
GOTO :EOF

:rmd
IF EXIST %1 rd /s /q %1
GOTO :EOF
