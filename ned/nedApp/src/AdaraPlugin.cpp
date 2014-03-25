#include "AdaraPlugin.h"

#include <iostream>
using namespace std;

EPICS_REGISTER_PLUGIN(AdaraPlugin, 2, "port name", string, "dispatcher port", string);

AdaraPlugin::AdaraPlugin(const char *portName, const char *dispatcherPortName)
    : BasePlugin(portName, dispatcherPortName, REASON_NORMAL)
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
