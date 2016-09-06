# lvPortDriver 1.0
EPICS device support for LabVIEW version

Copyright (c) 2016 LANS LLC, as Operator of
 Los Alamos National Laboratory.
 lvPortDriver is distributed subject to a Software License Agreement found
 in file LICENSE that is included with this distribution.
Primary Author: S. A. Baily
LA-CC-16-083

The EPICS build system will need access to the header files from
C:\Program Files x86\NationalInstruments\LabVIEW 2014\cintools\
The easiest way to accomplish this on a machine without LabVIEW installed is to
include these header files in the source directory of an EPICS shares application,
and install them as part of the make process.
If you are building for ms-windows, then you will also need the labview.lib from that
directory to be installed in lib/$(host_arch).
If you want to create cRIo applications then you will need the appropriate NI tool chain
from NI, but it will be simpler to configure the EPICS build system to use this compiler
than to configure NI's eclipse tool to build EPICS.

The LabVIEW pallete, libraries, and example code can be installed from the .vip file
using VI Package Manager.
