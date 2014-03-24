#include "AdaraPlugin.h"

#include <iostream>
using namespace std;

AdaraPlugin::AdaraPlugin(const char *portName, const char *dispatcherPortName, bool blockingCallbacks)
    : BasePlugin(portName, dispatcherPortName, blockingCallbacks, REASON_NORMAL)
{

}

AdaraPlugin::~AdaraPlugin()
{
}

const DasPacket *AdaraPlugin::processData(const DasPacketList * const packetList)
{
    cerr << "AdaraPlugin got data" << endl;
    return 0;
}
