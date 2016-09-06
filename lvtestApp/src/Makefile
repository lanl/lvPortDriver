TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application
SHARED_LIBRARIES=YES
STATIC_BUILD=NO

#PROD_IOC = lvtest

LOADABLE_LIBRARY = lvtest
# lvtest.dbd will be created and installed
DBD += lvtest.dbd

# lvtest.dbd will be made up from these files:
lvtest_DBD += base.dbd

# Include dbd files from all support applications:
lvtest_DBD += lvPortDriver.dbd
lvtest_DBD += asyn.dbd

# Add all the support libraries needed by this IOC
lvtest_LIBS += lvPortDriver
lvtest_LIBS += asyn

# lvtest_registerRecordDeviceDriver.cpp derives from lvtest.dbd
lvtest_SRCS += lvtest_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
lvtest_SRCS_DEFAULT += lvtestMain.cpp
lvtest_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#lvtest_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
lvtest_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

