/*************************************************************************\
* Copyright (c) 2016 LANS LLC, as Operator of
*  Los Alamos National Laboratory.
* lvPortDriver is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
* Primary Author: S. A. Baily
\*************************************************************************/
#include <asynPortDriver.h>
#ifdef _WIN32
#define DECLSPEC __declspec(dllexport)
#define _M_IX86
#else
#define DECLSPEC
#endif
// LabVIEW C interface here

#ifdef vxWorks
#define VXWORKS_PPC
#endif

#include "extcode.h"

#define MAX_LVPORTS 16

#define LVERROR_TYPE (asynStatus)(asynDisabled + 4)
#define LVERROR_ALLOCATION -1
#define LVERROR_COPY -2
#define LVERROR_CONNECT -3
#define LVERROR_FIND_INTERFACE -4
#define LVERROR_DRVINFO -5
#define LVERROR_CREATE_INTERFACE -6
#define LVERROR_REG_INTERRUPT -7
#define LVERROR_NO_MUTEX -8
#define LVERROR_UNKNOWN_PORT -9
#define LVERROR_PORT_RANGE -10

class lvPortDriver : public asynPortDriver {
public:
    lvPortDriver(const char *port,int numParams,int maxlists);
    ~lvPortDriver();
    virtual asynStatus readInt8Array (asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn);
    virtual asynStatus writeInt8Array (asynUser *pasynUser, epicsInt8 *value, size_t nElements);
    virtual asynStatus readInt16Array (asynUser *pasynUser, epicsInt16 *value, size_t nElements, size_t *nIn);
    virtual asynStatus writeInt16Array (asynUser *pasynUser, epicsInt16 *value, size_t nElements);
    virtual asynStatus readInt32Array (asynUser *pasynUser, epicsInt32 *value, size_t nElements, size_t *nIn);
    virtual asynStatus writeInt32Array (asynUser *pasynUser, epicsInt32 *value, size_t nElements);
    virtual asynStatus readFloat32Array (asynUser *pasynUser, epicsFloat32 *value, size_t nElements, size_t *nIn);
    virtual asynStatus writeFloat32Array (asynUser *pasynUser, epicsFloat32 *value, size_t nElements);
    virtual asynStatus readFloat64Array (asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn);
    virtual asynStatus writeFloat64Array (asynUser *pasynUser, epicsFloat64 *value, size_t nElements);
    virtual asynStatus readGenericPointer (asynUser *pasynUser, void *pointer);
    virtual asynStatus writeGenericPointer (asynUser *pasynUser, void *pointer);
    virtual asynStatus lvReadArray(int list, int index, asynParamType type, void ***handlePtr, int *status);  //used by LabVIEW C wrappers to read from an array
    virtual asynStatus lvWriteArray(int list, int index, asynParamType type, const void **handle, int status); //used by LabVIEW C wrappers to write to an array
    virtual void report(FILE *fp, int details);
protected:
    template <typename T, typename S>
    asynStatus writeArray(asynUser *pasynUser, T *value, size_t nBytes,NumType lvtype);
    template <typename T, typename S>
    asynStatus readArray (asynUser *pasynUser, T *value, int32 nBytes, size_t *nIn);
private:
    int maxParams;    //maximum number of parameters that can be created for this port
    void** *ptrTable; //table of handles used for array storage
};

static long drvlvPortDriver_io_report();
static long drvlvPortDriverInit();

//////////////////////////////////////////
//end from LabVIEW headers
extern "C" {
DECLSPEC  int lvPortDriverInit(const char *port,int numParams,int maxlists, int* index);  //create sn lvPortDriver object
DECLSPEC  void lvInitialized(); // inform driver that LabVIEW created all parameters.
DECLSPEC  int CreateParameter(int port,int type,const char *name,int *index);  //adds a parameter to the parameter list
DECLSPEC  int GetName(int port, int list, int Param,LStrHandle *portname, LStrHandle *paramname);  //gets the parameter and port names from  numerical indexes
DECLSPEC  int lvReadU32Digital(int port, int list, int index, unsigned mask, unsigned *value, int *status);  //writes to a parameter in the parameter list
DECLSPEC  int lvWriteU32Digital(int port, int list, int index, unsigned mask, unsigned value, int status); //reads from a parameter in the parameter list
DECLSPEC  int lvReadI32(int port, int list, int index, int *value, int *status); //reads from a parameter in the parameter list
DECLSPEC  int lvWriteI32(int port, int list, int index, int value, int status);   //writes to a parameter in the parameter list
DECLSPEC  int lvReadF64(int port, int list, int index, double *value, int *status); //reads from a parameter in the parameter list
DECLSPEC  int lvWriteF64(int port, int list, int index, double value, int status);   //writes to a parameter in the parameter list
DECLSPEC  int lvReadOctet(int port, int list, int index, int maxChars, char *value, int *status);   //reads from a string parameter in the parameter list
DECLSPEC  int lvWriteOctet(int port, int list, int index, const char *value, int status);   //writes to a string parameter in the parameter list
DECLSPEC  int lvWriteArray (int port, int list,int index, int type, const void **handle,int status);   //writes to an array parameter in the parameter list
DECLSPEC  int lvReadArray  (int port, int list,int index, int type, void ***handleptr,int *status);  //reads from an array parameter in the parameter list
DECLSPEC  int addevent (int port, int list, const char* param, LVUserEventRef* eventRefnum,int type,unsigned mask);  //sets up a callback that will post LabVIEW User events when a specified parameter changes
DECLSPEC  int debug_enableCallbacks(int enable); //used to enable Callbacks when the EPICS IOC will not be started.
}
