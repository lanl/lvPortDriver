/* lvtestMain.cpp */
/* Author:  Marty Kraimer Date:    17MAR2000 */
/* Modified by S. A. Baily 1/7/2016 */

#include <stddef.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "epicsExit.h"
#include "epicsThread.h"
#include "iocsh.h"

#include <fcntl.h>
#ifdef _WIN32
#include "windows.h" 
#include "winuser.h" 
extern "C" __declspec(dllexport) int callmain(char *startupscript)
{

    AllocConsole();
    freopen("CONOUT$","w",stdout);
    freopen("CONIN$","r",stdin);
    iocsh(startupscript);
    epicsThreadSleep(.2);
    iocsh(NULL);
    return(0);
}
#else
extern "C" int callmain(char *startupscript)
{
    freopen("/dev/IOC","r",stdin);
    freopen("/dev/IOC","w",stdout);
    freopen("/dev/IOC","w",stderr);
    iocsh(startupscript);
    epicsThreadSleep(.2);
    iocsh(NULL);
    return(0);
}
#endif

/*
int main(int argc,char *argv[])
{
    freopen("/dev/IOC","r",stdin);
    freopen("/dev/IOC","w",stdout);
    freopen("/dev/IOC","w",stderr);
    if(argc>=2) {    
        iocsh(argv[1]);
        epicsThreadSleep(.2);
    }
    iocsh(NULL);
    epicsExit(0);
    return(0);
}
*/