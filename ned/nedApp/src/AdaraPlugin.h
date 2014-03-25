#ifndef ADARA_PLUGIN_H
#define ADARA_PLUGIN_H

#include "BasePlugin.h"

class AdaraPlugin : public BasePlugin {
    public:
        AdaraPlugin(const char *portName, const char *dispatcherPortName);
        ~AdaraPlugin();

        const DasPacket *processData(const DasPacketList * const packetList);
};

#endif // ADARA_PLUGIN_H
