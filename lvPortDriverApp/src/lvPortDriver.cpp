/*************************************************************************\
* Copyright (c) 2016 LANS LLC, as Operator of
*  Los Alamos National Laboratory.
* lvPortDriver is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
* Primary Author: S. A. Baily
\*************************************************************************/

#include "lvPortDriver.h"
#include <string.h>
#include <algorithm>
#include <epicsThread.h>
#include <dbAccess.h> //needed for interruptAccept
#include <epicsMutex.h>
#include <epicsString.h>
#include <epicsEndian.h>
#include <epicsExport.h>

#ifdef _WIN32
 #include "windows.h"
 #define _WRS_CONSTRUCTOR(a, b) BOOL APIENTRY DllMain( HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpReserved)
 #define INITRETURN return TRUE;
#else
 #define INITRETURN
#endif

#if (EPICS_BYTE_ORDER == EPICS_ENDIAN_BIG)
#define HI 0
#define LO 1
#else /* EPICS_ENDIAN_LITTLE */
#define HI 1
#define LO 0
#endif /* EPICS_BYTE_ORDER */

typedef epicsMutex* epicsMutexPtr;
epicsMutexPtr lvPortDriver_Mutex=NULL;
epicsExportAddress(epicsMutexPtr,lvPortDriver_Mutex);

static lvPortDriver* lvInterfaces[MAX_LVPORTS];
static int numlvInterfaces=0;
static int LabVIEWinitialized=0;

#include <lv_prolog.h>


//I know labVIEW now has other names for these structures, but I can't find an appropriate header
typedef struct {
	int32 size;
	epicsInt32 data[1];
} lvaI32;
typedef struct {
	int32 size;
	epicsInt16 data[1];
} lvaI16;
typedef struct {
	int32 size;
	epicsInt8 data[1];
} lvaI8;
typedef struct {
	int32 size;
	epicsFloat32 data[1];
} lvaF32;
typedef struct {
	int32 size;
	epicsFloat64 data[1];
} lvaF64;

#ifdef _WIN32
extern "C" _WRS_CONSTRUCTOR(lvDLLInit, 101)
{
  if (ul_reason_for_call==DLL_PROCESS_DETACH && lvPortDriver_Mutex!=NULL) {
	//should removeInterruptUser, delete lvEventParams, and freeAsynUser 
	//for all events
    lvPortDriver_Mutex->lock();
    if (numlvInterfaces>MAX_LVPORTS) numlvInterfaces=MAX_LVPORTS;
    for (--numlvInterfaces;numlvInterfaces>=0;numlvInterfaces--)
        delete (lvInterfaces[numlvInterfaces]);  //this doesn't seem to be workign correctly
    lvPortDriver_Mutex->unlock();
    delete (lvPortDriver_Mutex); 
  }
  if (ul_reason_for_call!=DLL_PROCESS_ATTACH) INITRETURN
  if (lvPortDriver_Mutex!=NULL) printf ("lvsmLib already initialized\n");
  else {
    lvPortDriver_Mutex=new epicsMutex();
  }
  INITRETURN
}
#endif

// asynGenericPointerMask 
#define lvPortDriverTypeMask (asynInt32Mask | asynUInt32DigitalMask | asynFloat64Mask | asynOctetMask | asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynFloat32ArrayMask| asynFloat64ArrayMask)

lvPortDriver::lvPortDriver(const char *port,int numParams,int maxlists)
    :asynPortDriver(port,maxlists,numParams,
                    lvPortDriverTypeMask | asynOctetMask | asynDrvUserMask, //Interface mask
                    lvPortDriverTypeMask, // Interrupt mask
                    (maxlists>1)?ASYN_MULTIDEVICE:0, /* asynFlags ASYN_CANBLOCK=0 */
                    1, // Autoconnect 
                    0, // Default priority
                    0) // default stack size
{
    maxParams=numParams;
    ptrTable=new void**[maxAddr*maxParams]();
}

lvPortDriver::~lvPortDriver()
{
   if (epicsThreadGetId("asynPortDriverCallback") && !interruptAccept) { //this should be false if the IOC ran.
       interruptAccept=1;
       epicsThreadSleep(0.5);  //let callbacktask generate callbacks once so it will stop
       interruptAccept=0;
    }
    pasynManager->disconnect(pasynUserSelf);//should also delete labVIEW event data structures and unregister for callbacks
    pasynManager->freeAsynUser(pasynUserSelf);
    lock();
    for(int i=0;i<maxAddr;i++) 
      for(int j=0;j<maxParams;j++) {
        if (ptrTable[i*maxAddr+j]!=NULL) {
	    DSDisposeHandle (ptrTable[i*maxAddr+j]);  //LabVIEW array handles used to store data
        }
      }
    delete[] ptrTable;  
    unlock();
}

asynStatus lvPortDriver::readGenericPointer (asynUser *pasynUser, void *pointer)
{
	int list=0;
        asynStatus status;

	status=getAddress(pasynUser, &list);
        if (status != asynSuccess) return(status);
        lock();
	pointer=*ptrTable[list*maxAddr+pasynUser->reason];
        unlock();
	return (status);
}

asynStatus lvPortDriver::writeGenericPointer (asynUser *pasynUser, void *pointer)
{
	int list=0;
        asynStatus status;

	status=getAddress(pasynUser, &list); if (status != asynSuccess) return(status);
        lock();
	ptrTable[list*maxAddr+pasynUser->reason]=new void*(pointer);
        unlock();
        doCallbacksGenericPointer(pointer,pasynUser->reason, list);
        return (status);
}

template <typename T, typename S>
asynStatus lvPortDriver::writeArray(asynUser *pasynUser, T *value, size_t nBytes,NumType lvtype)
{
	int list=0;
	uChar*** handleptr;
        asynStatus astatus;
	MgErr status=noErr;
        int32 size=nBytes;
        switch (sizeof(T)) {
          case 8:
            size>>=3;
             break;
          case 4:
            size>>=2;
             break;
          case 2:
            size>>=1;
             break;
          default:
             break;
        }
	if (value==NULL) return (asynError);
	astatus=getAddress(pasynUser, &list); if (astatus != asynSuccess) return(astatus);
        lock();
	handleptr=(uChar***)&ptrTable[list*maxAddr+pasynUser->reason];

	status=NumericArrayResize (lvtype,1,handleptr,nBytes);  //(is this really bytes?) works even with Null handles
	if (status==noErr) {
	    ((S)**handleptr)->size=size;
            memcpy (((S)**handleptr)->data,value,nBytes);
        }
        unlock();
        switch (lvtype) {
          case iL:
            doCallbacksInt32Array(((lvaI32*) *handleptr)->data,(((lvaI32*) *handleptr)->size), pasynUser->reason, list);
            break;
          case iB:
            doCallbacksInt8Array(((lvaI8*) *handleptr)->data,(((lvaI8*) *handleptr)->size), pasynUser->reason, list);
            break;
          case iW:
            doCallbacksInt16Array(((lvaI16*) *handleptr)->data,(((lvaI16*) *handleptr)->size), pasynUser->reason, list);
            break;
          case fS:
            doCallbacksFloat32Array(((lvaF32*) *handleptr)->data,(((lvaF32*) *handleptr)->size), pasynUser->reason, list);
            break;
          case fD:
            doCallbacksFloat64Array(((lvaF64*) *handleptr)->data,(((lvaF64*) *handleptr)->size), pasynUser->reason, list);
            break;
          default:
            break;
        }
        if (status==noErr) return (asynSuccess);
	return (asynError);
}

template <typename T, typename S>
asynStatus lvPortDriver::readArray (asynUser *pasynUser, T *value, int32 nBytes, size_t *nIn)
{
	int list=0;
        int32 size;
	S *handle;
        asynStatus status;

	if (value==NULL) return (asynError);
	status=getAddress(pasynUser, &list);
        if (status != asynSuccess) return(status);
        lock();
	handle=(S *)ptrTable[list*maxAddr+pasynUser->reason];
	if (handle==NULL || *handle==NULL) {
            unlock();
            return (asynError);
        }
        size=(*handle)->size;
        switch (sizeof(T)) {
          case 8:
            size<<=3;
             break;
          case 4:
            size<<=2;
             break;
          case 2:
            size<<=1;
             break;
          default:
             break;
        }
	size=std::min(nBytes,size);
        memcpy(value,(*handle)->data,size);
        unlock();
        switch (sizeof(T)) {
          case 8:
            *nIn=size>>3;
             break;
          case 4:
            *nIn=size>>2;
             break;
          case 2:
            *nIn=size>>1;
             break;
          default:
            *nIn=size;
        }
	return (status);
};

asynStatus lvPortDriver::writeInt8Array (asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
        return(writeArray<epicsInt8,lvaI8*>(pasynUser,value,nElements,iB));
}

asynStatus lvPortDriver::readInt8Array (asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)
{
    return (readArray<epicsInt8,lvaI8*>(pasynUser,value,nElements,nIn));
}

asynStatus lvPortDriver::writeInt16Array (asynUser *pasynUser, epicsInt16 *value, size_t nElements)
{
    return(writeArray<epicsInt16,lvaI16*>(pasynUser,value, nElements<<1,iB));
}

asynStatus lvPortDriver::readInt16Array (asynUser *pasynUser, epicsInt16 *value, size_t nElements, size_t *nIn)
{
    return (readArray<epicsInt16,lvaI16*>(pasynUser,value,nElements<<1,nIn));
}

asynStatus lvPortDriver::writeInt32Array (asynUser *pasynUser, epicsInt32 *value, size_t nElements)
{
    return(writeArray<epicsInt32,lvaI32*>(pasynUser,value, nElements<<2,iL));

}

asynStatus lvPortDriver::readInt32Array (asynUser *pasynUser, epicsInt32 *value, size_t nElements, size_t *nIn)
{
    return (readArray<epicsInt32,lvaI32*>(pasynUser,value,nElements<<2,nIn));
}

asynStatus lvPortDriver::writeFloat32Array (asynUser *pasynUser, epicsFloat32 *value, size_t nElements)
{
    return(writeArray<epicsFloat32,lvaF32*>(pasynUser,value, nElements<<2,fS));
}

asynStatus lvPortDriver::readFloat32Array (asynUser *pasynUser, epicsFloat32 *value, size_t nElements, size_t *nIn)
{
    return (readArray<epicsFloat32,lvaF32*>(pasynUser,value,nElements<<2,nIn));
}

asynStatus lvPortDriver::writeFloat64Array (asynUser *pasynUser, epicsFloat64 *value, size_t nElements)
{
    return(writeArray<epicsFloat64,lvaF64*>(pasynUser,value, nElements<<3,fD));
}

asynStatus lvPortDriver::readFloat64Array (asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn)
{
    return (readArray<epicsFloat64,lvaF64*>(pasynUser,value,nElements<<3,nIn));
}

asynStatus lvPortDriver::lvWriteArray(int list, int index, asynParamType type, const void **handle, int status)
{
    MgErr mgstatus=noErr;
    asynStatus astatus=asynError;
    uChar*** handleptr;
    NumType lvtype;
    epicsInt32 datasize=0;

    if (handle==NULL || *handle==NULL) return (asynError);
    datasize=((lvaI32*) *handle)->size; //size is in the same place for all types of array structures
    lock();
    handleptr=(uChar***)&ptrTable[list*maxAddr+index];
    switch (type) {
      case asynParamInt8Array:
        lvtype=iB;
        break;
      case asynParamInt16Array:
        lvtype=iW;
        datasize<<=1;
        break;
      case asynParamInt32Array:
        lvtype=iL;
        datasize<<=2;
        break;
      case asynParamFloat32Array:
        lvtype=fS;
        datasize<<=2;
        break;
      case asynParamFloat64Array:
        lvtype=fD;
        datasize<<=3;
        break;
      default:
        unlock();
        return(asynError);
        break;
    }
    if (*handleptr!=NULL) {
        mgstatus=NumericArrayResize (lvtype,1,handleptr,datasize); //is this necessary? is this really in bytes?
        if (mgstatus==noErr) { 
            ((lvaI32*) **handleptr)->size=((lvaI32*) *handle)->size; //is this necessary?
        } 
    } //will dscopyhandle take care of this?
    if (mgstatus==noErr) { 
        mgstatus=DSCopyHandle(handleptr,handle);
    }
    unlock();
    if (mgstatus==noErr) astatus=setParamStatus(list,index,(asynStatus)status);
    else setParamStatus(list,index,asynError);
    if (*handleptr==NULL) return (asynError);
    switch (type) {
      case asynParamInt8Array:
        doCallbacksInt8Array(((lvaI8*) **handleptr)->data,((lvaI8*) **handleptr)->size, index, list);
        break;
      case asynParamInt16Array:
        doCallbacksInt16Array(((lvaI16*) **handleptr)->data,((lvaI16*) **handleptr)->size, index, list);
        break;
      case asynParamInt32Array:
        doCallbacksInt32Array(((lvaI32*) **handleptr)->data,((lvaI32*) **handleptr)->size, index, list);
        break;
      case asynParamFloat32Array:
        doCallbacksFloat32Array(((lvaF32*) **handleptr)->data,((lvaF32*) **handleptr)->size, index, list);
        break;
      case asynParamFloat64Array:
        doCallbacksFloat64Array(((lvaF64*) **handleptr)->data,((lvaF64*) **handleptr)->size, index, list);
        break;
      default:
        break;
    }
    return (astatus);
}

asynStatus lvPortDriver::lvReadArray(int list, int index, asynParamType type, void ***handlePtr, int *status)
{
    MgErr mgstatus=noErr;
    asynStatus astatus=asynError;
    asynStatus asynstatus=(asynStatus)*status;
    uChar*** handleptr;
    NumType lvtype;
    size_t size,datasize;

    if (handlePtr==NULL) return (asynError);
    lock();
    handleptr=ptrTable?(uChar***)&ptrTable[list*maxAddr+index]:NULL;
    if (handleptr==NULL || *handleptr==NULL) size=0;
    else size=((lvaI32*) **handleptr)->size;
        switch (type) {
          case asynParamInt8Array:
            lvtype=iB;
            datasize=size;
            break;
          case asynParamInt16Array:
            lvtype=iW;
            datasize=size<<1;
            break;
          case asynParamInt32Array:
            lvtype=iL;
            datasize=size<<2;
            break;
          case asynParamFloat32Array:
            lvtype=fS;
            datasize=size<<2;
            break;
          case asynParamFloat64Array:
            lvtype=fD;
            datasize=size<<3;
            break;
          default:
            unlock();
            return(asynError);
            break;
        }
    if (*handlePtr!=NULL) {
        mgstatus=NumericArrayResize(lvtype,1,(uChar***)handlePtr,datasize); //is this necessary?
        if (mgstatus==noErr) { 
            ((lvaI32*) **handlePtr)->size=size; //is this necessary?
        }
    }  //will DScopyHandle take care of this?
   if (handleptr!=NULL && *handleptr!=NULL && mgstatus==noErr) {
        mgstatus=DSCopyHandle(handlePtr,*handleptr);
    }
    unlock();
    if (mgstatus==noErr) {
        astatus=getParamStatus(list,index,&asynstatus);
        *status=asynstatus;
    } else astatus=asynError;
    return (astatus);
}
 
void lvPortDriver::report(FILE *fp, int details)
{
    MgErr invalid;
    uChar** handle;
    int size;
    asynPortDriver::report(fp,details); // Invoke the base class method
    fprintf(fp,"LabVIEW Port Driver %s.\n", this->portName);
    if (details>2) {
        fprintf(fp,"ptrTable Contents\n");
	if (ptrTable!=NULL) {
            for (int i=0;i<maxAddr*maxParams;i++) {
              handle=(uChar**)ptrTable[i];
              invalid=(handle!=NULL)?DSCheckHandle(handle):0;
              fprintf(fp,"%i: %p : %li",i,handle,(long int)invalid);
              if (details>3 && handle!=NULL && *handle!=NULL && invalid==0) {
                  size=((lvaI32*) *handle)->size;
                  fprintf(fp," %i",size);
                  for (int i=0;i<std::min(size,3);i++) fprintf(fp," %i",((lvaI32*) *handle)->data[i]);
              }
              fprintf(fp,"\n");
            }
        }
    }
}

struct drvet {
	long number;
	long (*report)();
	long (*init)();
} drvlvPortDriver = {
        2,
        drvlvPortDriver_io_report,
        drvlvPortDriverInit
};
epicsExportAddress(drvet, drvlvPortDriver);
// alternatively one could call runLabVIEW from the shell (possibly giving it a VI name),
// but having LabVIEW exported as a driver lets it start automatically


static long drvlvPortDriver_io_report(void)
    // report details about LabVIEW (running? version? etc.)
{
    return(0);
}


static long drvlvPortDriverInit(void)
    // start LabVIEW main program and wait for parameter initialization
    // this may need to start a new thread
{
    if (lvPortDriver_Mutex==NULL) lvPortDriver_Mutex=new epicsMutex();
    int count=0;
    long status=0;
    while (!status && count++ <30) {
            epicsThreadSleep(1);
            lvPortDriver_Mutex->lock();
            status=LabVIEWinitialized;         
            lvPortDriver_Mutex->unlock();
    }
    if (count>=30) printf("lvPortDriver Error: LabVIEW program did not initialize.\n");
    return (0);
}


//c client interface
typedef struct lvEventParams {
    LVUserEventRef Refnum;
    epicsInt32 port;
    epicsInt32 list;
    epicsInt32 type; //(asynParamType) maybe not necessary
    const char *portName;
    const char *paramName;
    epicsUInt32 mask;
} lvEventParams;

typedef union {
        epicsInt32 I32[2];
        epicsUInt32 U32[2];
        epicsFloat64 F64;
	const void* ptr;
} LVdataunion;

typedef struct lvEventdata {
    epicsInt32 port;
    epicsInt32 list;
    epicsInt32 type; //(asynParamType)
    epicsInt32 param;
    epicsInt32 status; //asynStatus
    LStrHandle string;
    LVdataunion value;
} lvEventdata;


static LStrHandle stringToNewLStrHandle(LStrHandle *handle,const char* string)
{
    if (string==NULL) return (NULL);
    epicsUInt32 length=strlen(string);
    *handle=(LStrHandle)DSNewHandle(sizeof(int32)+length*sizeof(uChar));
    strncpy((char*)LStrBuf(**handle),string,length);
    LStrLen(**handle)=length;
    return (*handle);
} 

static void callbackInform(void *userPvt, asynUser *pasynUser, lvEventdata* lvdata)
//does this now have a status parameter, or do we use pasynUser->auxStatus?
//should we include timestamps?
{
    lvEventParams* lvparams=(lvEventParams*)userPvt;
    lvdata->port=lvparams->port;
    lvdata->list=lvparams->list;
    lvdata->param=pasynUser->reason;
    lvdata->status=pasynUser->auxStatus;
    lvdata->type=lvparams->type;
    PostLVUserEvent(lvparams->Refnum,(void *)lvdata);
}

template <typename T>
static void callbackarray(void *userPvt, asynUser *pasynUser, T *value, size_t nelements)
//Must use register for event callback if you want to copy data using moveblock.
//otherwise just do a read to get the latest value (but intermediate values might be dropped when busy)
//should we include timestamps?
{
    lvEventdata lvdata;
    size_t datasize=nelements*sizeof(T);
    lvdata.string=(LStrHandle)DSNewHandle(sizeof(int32)+datasize);
    if (lvdata.string!=NULL) {
        memcpy((char*)LStrBuf(*lvdata.string),value,datasize);
        LStrLen(*lvdata.string)=datasize;
    }
    lvdata.value.U32[HI]=0;
    lvdata.value.U32[LO]=0;
    lvdata.value.ptr=NULL;
    callbackInform(userPvt, pasynUser, &lvdata);
    DSDisposeHandle(lvdata.string);
}

static void callbackO(void *userPvt, asynUser *pasynUser, char *data, size_t numchars, int eomReason)
//should we include timestamps?
{
    lvEventdata lvdata;
    lvdata.string=(LStrHandle)DSNewHandle(sizeof(int32)+numchars);
    if (lvdata.string!=NULL) {
        memcpy((char*)LStrBuf(*lvdata.string),data,numchars);
        LStrLen(*lvdata.string)=numchars;
    }
    lvdata.value.I32[HI]=eomReason;
    lvdata.value.I32[LO]=0;
    callbackInform(userPvt, pasynUser, &lvdata);
    DSDisposeHandle(lvdata.string);
}

static void callbackI32(void *userPvt, asynUser *pasynUser, epicsInt32 data)
//should we include timestamps?
{
    lvEventdata lvdata;
    lvdata.string=NULL;
    lvdata.value.I32[HI]=data;
    lvdata.value.I32[LO]=0;
    callbackInform(userPvt, pasynUser, &lvdata);
}
static void callbackU32(void *userPvt, asynUser *pasynUser, epicsUInt32 data)
//should we include timestamps?
{
    lvEventParams* lvparams=(lvEventParams*)userPvt;
    lvEventdata lvdata;
    lvdata.string=NULL;
    lvdata.value.U32[HI]=data;
    lvdata.value.U32[LO]=lvparams->mask;
    callbackInform(userPvt, pasynUser, &lvdata);
}
static void callbackF64(void *userPvt, asynUser *pasynUser, epicsFloat64 data)
//should we include timestamps?
{
    lvEventdata lvdata;
    lvdata.string=NULL;
    lvdata.value.F64=data;
    callbackInform(userPvt, pasynUser, &lvdata);
}
static void callbackptr(void *userPvt, asynUser *pasynUser, void *pointer)
//should we include timestamps?
{
    lvEventdata lvdata;
    lvdata.string=NULL;
    lvdata.type=asynParamGenericPointer;
    lvdata.value.U32[HI]=0;
    lvdata.value.U32[LO]=0;
    lvdata.value.ptr=pointer;
    callbackInform(userPvt, pasynUser, &lvdata);
}

static const char I32string[]=asynInt32Type;
static const char U32string[]=asynUInt32DigitalType;
static const char F64string[]=asynFloat64Type;
static const char Ostring[]=asynOctetType;
static const char PTRstring[]=asynGenericPointerType;
static const char I32astring[]=asynInt32ArrayType;
static const char I16astring[]=asynInt16ArrayType;
static const char I8astring[]=asynInt8ArrayType;
static const char F32astring[]=asynFloat32ArrayType;
static const char F64astring[]=asynFloat64ArrayType;

extern "C" int addevent (int port, int list, const char* param, LVUserEventRef* eventRefnum,int type,unsigned mask)
//mask only used for UInt32Digital
{
    asynStatus status=asynSuccess;
    asynInterface *pasynInterface;
    asynInterface *pinterface;
    const char* asynInterfaceType;
    asynDrvUser *pDrvUser;
    void *interruptPvt;
    char *drvInfo=NULL;
    asynUser *pasynUser;
    lvEventParams *plvEventParams;
	
    switch ((asynParamType) type) {
      case asynParamInt32:
        asynInterfaceType=I32string;
        break;
      case asynParamUInt32Digital:
        asynInterfaceType=U32string;
        break;
     case asynParamFloat64:
        asynInterfaceType=F64string;
        break;
     case asynParamOctet:
        asynInterfaceType=Ostring;
        break;
      case asynParamGenericPointer:
        asynInterfaceType=PTRstring;
        break;
      case asynParamInt32Array:
        asynInterfaceType=I32astring;
        break;
      case asynParamInt16Array:
        asynInterfaceType=I16astring;
        break;
      case asynParamInt8Array:
        asynInterfaceType=I8astring;
        break;
      case asynParamFloat32Array:
        asynInterfaceType=F32astring;
        break;
      case asynParamFloat64Array:
        asynInterfaceType=F64astring;
        break;
     default:
        return (LVERROR_TYPE);
    };	
    plvEventParams =new lvEventParams;
    if (plvEventParams==NULL) return (LVERROR_ALLOCATION);
    plvEventParams->Refnum=*eventRefnum;
    plvEventParams->port=port;
    plvEventParams->list=list;
    plvEventParams->type=type;
   
    if (param) {
        drvInfo=epicsStrDup(param);
        plvEventParams->paramName=epicsStrDup(param);
    }
    pasynUser = pasynManager->createAsynUser(0,0);

    lvPortDriver_Mutex->lock();
    if ( (port<0) || (port>=numlvInterfaces)) status=asynError;
    lvPortDriver_Mutex->unlock();
    plvEventParams->portName=epicsStrDup(lvInterfaces[port]->portName);
    if (status) return (LVERROR_COPY);
    status = pasynManager->connectDevice(pasynUser, plvEventParams->portName, list);
    if (status) return (LVERROR_CONNECT);
    pasynInterface = pasynManager->findInterface(pasynUser, asynInterfaceType, 1);    
    if (status) return (LVERROR_FIND_INTERFACE);
    if (!drvInfo) return (LVERROR_DRVINFO);
    pinterface = pasynManager->findInterface(pasynUser, asynDrvUserType, 1);
    if (!pinterface) return (LVERROR_FIND_INTERFACE);
    pDrvUser=(asynDrvUser *)pinterface->pinterface;
    status = pDrvUser->create(pinterface->drvPvt, pasynUser, drvInfo, 0, 0);
    if (plvEventParams==NULL) return (LVERROR_CREATE_INTERFACE);
    switch ((asynParamType) type) {
      case asynParamInt32:
        status=((asynInt32*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackI32, plvEventParams, &interruptPvt);
        break;
      case asynParamUInt32Digital:
	plvEventParams->mask=mask;
        status=((asynUInt32Digital*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackU32, plvEventParams,plvEventParams->mask, &interruptPvt);
        break;
      case asynParamFloat64:
        status=((asynFloat64*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackF64, plvEventParams, &interruptPvt);
        break;
      case asynParamOctet:
        status=((asynOctet*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackO, plvEventParams, &interruptPvt);
        break;
      case asynParamGenericPointer:
        status=((asynGenericPointer*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackptr, plvEventParams, &interruptPvt);
        break;
      case asynParamInt32Array:
        status=((asynInt32Array*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackarray, plvEventParams, &interruptPvt);
        break;
      case asynParamInt16Array:
        status=((asynInt16Array*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackarray, plvEventParams, &interruptPvt);
        break;
      case asynParamInt8Array:
        status=((asynInt8Array*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackarray, plvEventParams, &interruptPvt);
        break;
      case asynParamFloat32Array:
        status=((asynFloat32Array*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackarray, plvEventParams, &interruptPvt);
        break;
      case asynParamFloat64Array:
        status=((asynFloat64Array*)(pasynInterface->pinterface))->registerInterruptUser(pasynInterface->drvPvt, pasynUser,
                                                  &callbackarray, plvEventParams, &interruptPvt);
        break;
      default:
        status=LVERROR_TYPE;
    };
    if (status) return (LVERROR_REG_INTERRUPT);                                                  
    return (0);
}

//c interface functions
extern "C" void lvInitialized() // inform driver that LabVIEW created all parameters.
{
    lvPortDriver_Mutex->lock();
    LabVIEWinitialized=1;
    lvPortDriver_Mutex->unlock();
}

extern "C" int lvPortDriverInit(const char *port,int numParams,int maxlists,int *index)
{
    int status=0;
    if (lvPortDriver_Mutex==NULL) lvPortDriver_Mutex=new epicsMutex;
    lvPortDriver_Mutex->lock();
    if (numlvInterfaces > MAX_LVPORTS) status=LVERROR_PORT_RANGE;
    else {
	lvInterfaces[numlvInterfaces]=new lvPortDriver(port,numParams,maxlists);
	if (NULL==lvInterfaces[numlvInterfaces]) status=-1;
	else {
            *index=numlvInterfaces;
	    numlvInterfaces++;
            status=0;
        }
    }
    lvPortDriver_Mutex->unlock(); 
    return (status);
}

extern "C" int CreateParameter(int port,int type,const char *name,int *index)
//creates parameter for all lists
{
    int status;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) status=LVERROR_UNKNOWN_PORT;
    else status=lvInterfaces[port]->createParam(name,(asynParamType)type,index);
    lvPortDriver_Mutex->unlock();
    return (status);
}

static int lvRead(int port, int list, int index, asynParamType type, unsigned mask, LVdataunion *value, int *status)
{
    int portstatus;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) portstatus=LVERROR_UNKNOWN_PORT;
    else {
        switch (type) {
          case asynParamInt32:
            portstatus=lvInterfaces[port]->getIntegerParam(list, index, value->I32);
            break;
          case asynParamUInt32Digital:
            portstatus=lvInterfaces[port]->getUIntDigitalParam(list, index, value->U32, mask);
            break;
          case asynParamFloat64:
            portstatus=lvInterfaces[port]->getDoubleParam(list, index, &(value->F64));
            break;

          default:
            portstatus = asynError;
        }
        *status=portstatus;
    }
    lvPortDriver_Mutex->unlock();
    return (portstatus);
}
extern "C" int lvReadI32(int port, int list, int index, int *value, int *status) {
    LVdataunion lvdata;
    int portstatus;
    portstatus=lvRead(port, list, index, asynParamInt32, 0, &lvdata, status);
    *value=lvdata.I32[0];
    return (portstatus);    
}
extern "C" int lvReadU32Digital(int port, int list, int index, unsigned mask, unsigned *value, int *status) {
    LVdataunion lvdata;
    int portstatus;
    portstatus=lvRead(port, list, index, asynParamUInt32Digital, mask, &lvdata, status);
    *value=lvdata.U32[0];
    return (portstatus);    
}
extern "C" int lvReadF64(int port, int list, int index, double *value, int *status) {
    LVdataunion lvdata;
    int portstatus;
    portstatus=lvRead(port, list, index, asynParamFloat64, 0, &lvdata, status);
    *value=lvdata.F64;
    return (portstatus);    
}

extern "C" int lvReadOctet(int port, int list, int index, int maxChars, char *value, int *status) {
    int portstatus;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) portstatus=LVERROR_UNKNOWN_PORT;
    else {
        portstatus=lvInterfaces[port]->getStringParam(list, index, maxChars, value);
        *status=portstatus;
    }
    lvPortDriver_Mutex->unlock();
    return (portstatus);
}

static int lvWrite(int port, int list, int index, asynParamType type, unsigned mask, LVdataunion value, int status)
{
    int portstatus;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) portstatus=LVERROR_UNKNOWN_PORT;
    else {
        switch (type) {
          case asynParamInt32:
            portstatus=lvInterfaces[port]->setIntegerParam(list, index, value.I32[0]);
            break;
          case asynParamUInt32Digital:
            portstatus=lvInterfaces[port]->setUIntDigitalParam(list, index, value.U32[0], mask);
            break;
          case asynParamFloat64:
            portstatus=lvInterfaces[port]->setDoubleParam(list, index, value.F64);
            break;
          case asynParamOctet:
            portstatus=lvInterfaces[port]->setStringParam(list, index, (char *)value.ptr);
            break;

          default:
            portstatus = asynError;
        }
        if (portstatus == asynSuccess) portstatus=lvInterfaces[port]->setParamStatus(list,index,(asynStatus)status);
        if (portstatus == asynSuccess) portstatus=lvInterfaces[port]->callParamCallbacks(list,list);
    }
    lvPortDriver_Mutex->unlock();
    return (portstatus);
}

extern "C" int lvWriteI32(int port, int list, int index, int value, int status)
{
    LVdataunion lvdata;
    lvdata.I32[0]=value;
    return(lvWrite(port,list,index,asynParamInt32,0,lvdata,status));
}

extern "C" int lvWriteU32Digital(int port, int list, int index, unsigned mask, unsigned value, int status)
{
    LVdataunion lvdata;
    lvdata.U32[0]=value;
    return(lvWrite(port,list,index,asynParamUInt32Digital,mask,lvdata,status));
}

extern "C" int lvWriteF64(int port, int list, int index, double value, int status)
{
    LVdataunion lvdata;
    lvdata.F64=value;
    return(lvWrite(port,list,index,asynParamFloat64,0,lvdata,status));
}

extern "C" int lvWriteOctet(int port, int list, int index, const char *value, int status)
{
    LVdataunion lvdata;
    lvdata.ptr=value;
    return(lvWrite(port,list,index,asynParamOctet,0,lvdata,status));
}

extern "C" int lvWriteArray (int port, int list,int index, int type, const void **handle, int status)
{
    int portstatus;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) portstatus=LVERROR_UNKNOWN_PORT;
    else {
        portstatus=lvInterfaces[port]->lvWriteArray(list, index, (asynParamType) type, handle, status);
    }
    lvPortDriver_Mutex->unlock();
    return (portstatus);

}

extern "C" int lvReadArray  (int port, int list,int index, int type, void ***handleptr,int *status)
{
    int portstatus;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) portstatus=LVERROR_UNKNOWN_PORT;
    else {
        portstatus=lvInterfaces[port]->lvReadArray(list, index, (asynParamType) type, handleptr, status);
    }
    lvPortDriver_Mutex->unlock();
    return (portstatus);
}

extern "C" int GetName(int port, int list, int Param, LStrHandle *portname, LStrHandle *paramname)
//paramname and portname should be called with NULL strings
{
    const char *parametername=NULL;
    int status=0;
    if (lvPortDriver_Mutex==NULL) return (LVERROR_NO_MUTEX);
    lvPortDriver_Mutex->lock();
    if (port>=numlvInterfaces) status=LVERROR_UNKNOWN_PORT;
    else {
        if (NULL==stringToNewLStrHandle(portname,lvInterfaces[port]->portName)) return (LVERROR_ALLOCATION);
        if (Param>=0) status=lvInterfaces[port]->getParamName(list,Param,&parametername);
    }
    if (!status && NULL==stringToNewLStrHandle(paramname,parametername)) return (LVERROR_ALLOCATION);
    lvPortDriver_Mutex->unlock(); 
    return (status);
    
}
extern "C" int debug_enableCallbacks(int enable) {
       interruptAccept=enable?1:0;
	return(0);
}
#include <lv_epilog.h>
