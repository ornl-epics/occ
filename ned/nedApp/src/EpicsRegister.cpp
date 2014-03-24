#include "OccPortDriver.h"

#include <epicsExport.h>
#include <epicsExit.h>
#include <iocsh.h>

static OccPortDriver *drv = NULL;

extern "C" {
    void nedTeardown(void *)
    {
        if (drv != NULL) {
            delete drv;
            drv = NULL;
        }
    }

    int nedSetup(const char *portName, int deviceId, int bufferSize)
    {
        drv = new OccPortDriver(portName, deviceId, bufferSize);
        epicsAtExit(nedTeardown, NULL);
        return asynSuccess;
    }
}

/* Code for iocsh registration */
static const iocshArg nedSetupArg0 = {"Port name", iocshArgString};
static const iocshArg nedSetupArg1 = {"Device id", iocshArgInt};
static const iocshArg nedSetupArg2 = {"Local buffer size", iocshArgInt};
static const iocshArg * const nedSetupArgs[] =  {&nedSetupArg0,
                                                 &nedSetupArg1,
                                                 &nedSetupArg2};
static const iocshFuncDef setupNed = {"nedSetup", 3, nedSetupArgs};
static void configNedCallFunc(const iocshArgBuf *args)
{
    nedSetup(args[0].sval, args[1].ival, args[2].ival);
}

static void nedRegister(void)
{
    iocshRegister(&setupNed, configNedCallFunc);
}

extern "C" {
    epicsExportRegistrar(nedRegister);
}
