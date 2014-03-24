#include "BaseDispatcher.h"

static const int asynMaxAddr       = 1;
static const int asynInterfaceMask = asynInt32Mask | asynGenericPointerMask | asynDrvUserMask; // don't remove DrvUserMask or you'll break callback's reasons
static const int asynInterruptMask = asynInt32Mask | asynGenericPointerMask;
static const int asynFlags         = 0;
static const int asynAutoConnect   = 1;
static const int asynPriority      = 0;
static const int asynStackSize     = 0;

#define NUM_BASEDISPATCHER_PARAMS ((int)(&LAST_BASEDISPATCHER_PARAM - &FIRST_BASEDISPATCHER_PARAM + 1))

BaseDispatcher::BaseDispatcher(const char *portName)
	: asynPortDriver(portName, asynMaxAddr, NUM_BASEDISPATCHER_PARAMS, asynInterfaceMask,
	                 asynInterruptMask, asynFlags, asynAutoConnect, asynPriority, asynStackSize)
{

}

void BaseDispatcher::sendToPlugins(int messageType, const DasPacketList *packetList)
{
    const void *addr = reinterpret_cast<const void *>(packetList);
    doCallbacksGenericPointer(const_cast<void *>(addr), messageType, 0);
}
