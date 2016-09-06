#!../../bin/win32-x86-mingw/lvtest

## You may have to change lvtest to something else
## everywhere it appears in this file
cd "C:\epics\ioctop\iocBoot\ioc_lvtest"
< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase "dbd/lvtest.dbd"
lvtest_registerRecordDeviceDriver pdbbase

## Load record instances
dbLoadRecords("db/lvtest.db","user=218119Host")

cd ${TOP}/iocBoot/${IOC}
iocInit

## Start any sequence programs
#seq sncxxx,"user=218119Host"
