#include "ProxyPlugin.h"

#include <poll.h>
#include <osiSock.h>
#include <string.h> // strerror

EPICS_REGISTER_PLUGIN(ProxyPlugin, 3, "port name", string, "dispatcher port", string, "blocking callbacks", int);

#define NUM_PROXYPLUGIN_PARAMS      0

ProxyPlugin::ProxyPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BaseSocketPlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_PROXYPLUGIN_PARAMS)
    , m_nTransmitted(0)
    , m_nProcessed(0)
    , m_nReceived(0)
{
}

ProxyPlugin::~ProxyPlugin()
{
}

void ProxyPlugin::processData(const DasPacketList * const packetList)
{
    // Do we need to connect the client? There's no extra thread that would wait
    // for client, instead we rely on the incoming data rate to trigger this function
    // quite often.
    bool clientConnected = connectClient();

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_nReceived++;

        // Don't even bother with packet inspection if there's noone interested
        if (!clientConnected)
            continue;

       if (send(reinterpret_cast<const uint32_t *>(packet), packet->length()))
            m_nTransmitted++;
        m_nProcessed++;
    }

    // Update parameters
    setIntegerParam(TxCount,    m_nTransmitted);
    setIntegerParam(ProcCount,  m_nProcessed);
    setIntegerParam(RxCount,    m_nReceived);
    callParamCallbacks();
}
