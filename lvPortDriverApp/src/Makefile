TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

#LIBRARY_IOC += lvPortDriver
#note if loadable library is present code will not be built into the IOC
#even for static builds.
#LOADABLE_LIBRARY += lvPortDriver
LIBRARY += lvPortDriver

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install lvPortDriver.dbd into <top>/dbd
DBD += lvPortDriver.dbd

# specify all source files to be compiled and added to the library
lvPortDriver_SRCS += lvPortDriver.cpp

lvPortDriver_LIBS_WIN32 += labview
lvPortDriver_LIBS += asyn
lvPortDriver_LIBS += $(EPICS_BASE_IOC_LIBS)

#==================================================
# Install all protocol files in the data directory

DATA += $(patsubst ../%, %, $(wildcard ../*.proto))

#===========================
INC += lvPortDriver.h

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

