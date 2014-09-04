#include "GenericModulePlugin.h"
#include "Log.h"

#include <algorithm>

EPICS_REGISTER_PLUGIN(GenericModulePlugin, 2, "Port name", string, "Dispatcher port name", string);

#define NUM_GENERICMODULEPLUGIN_PARAMS      ((int)(&LAST_GENERICMODULEPLUGIN_PARAM - &FIRST_GENERICMODULEPLUGIN_PARAM + 1))

GenericModulePlugin::GenericModulePlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_GENERICMODULEPLUGIN_PARAMS, 1,
                 defaultInterfaceMask, defaultInterruptMask)
    , m_hardwareId(0)
    , m_payloadLen(0)
{
    createParam("ReqDest",      asynParamOctet, &ReqDest);
    createParam("ReqCmd",       asynParamInt32, &ReqCmd);
    createParam("ReqIsDsp",     asynParamInt32, &ReqIsDsp);
    createParam("RspCmd",       asynParamInt32, &RspCmd);
    createParam("RspCmdAck",    asynParamInt32, &RspCmdAck);
    createParam("RspHwType",    asynParamInt32, &RspHwType);
    createParam("RspSrc",       asynParamOctet, &RspSrc);
    createParam("RspRouter",    asynParamOctet, &RspRouter);
    createParam("RspDest",      asynParamOctet, &RspDest);
    createParam("RspLen",       asynParamInt32, &RspLen);
    createParam("RspDataLen",   asynParamInt32, &RspDataLen);
    createParam("RspData",      asynParamOctet, &RspData);

    callParamCallbacks();
}

asynStatus GenericModulePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == ReqCmd) {
        // Allow any value from the client.
        request(static_cast<const DasPacket::CommandType>(value & 0xFF));
        return asynSuccess;
    }
    return BasePlugin::writeInt32(pasynUser, value);
}

asynStatus GenericModulePlugin::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
    if (pasynUser->reason == ReqDest)
        m_hardwareId = BaseModulePlugin::ip2addr(std::string(value, nChars));
    return BasePlugin::writeOctet(pasynUser, value, nChars, nActual);;
}

asynStatus GenericModulePlugin::readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason)
{
    if (pasynUser->reason == RspData) {
        *nActual = 0;
        for (uint32_t i = 0; i < m_payloadLen; i++) {
            int len = snprintf(value, nChars, "0x%08X ", m_payload[i]);
            if (len >= static_cast<int>(nChars) || len == -1)
                break;
            nChars -= len;
            *nActual += len;
            value += len;
        }
        if (eomReason) *eomReason |= ASYN_EOM_EOS;
        return asynSuccess;
    }
    return BasePlugin::readOctet(pasynUser, value, nChars, nActual, eomReason);
}

void GenericModulePlugin::request(const DasPacket::CommandType command)
{
    DasPacket *packet;
    int isDsp;

    if (m_hardwareId == 0)
        return;

    (void)getIntegerParam(ReqIsDsp, &isDsp);

    if (isDsp == 1)
        packet = DasPacket::createOcc(DasPacket::HWID_SELF, m_hardwareId, command, 0);
    else
        packet = DasPacket::createLvds(DasPacket::HWID_SELF, m_hardwareId, command, 0);

    if (packet) {
        BasePlugin::sendToDispatcher(packet);
        delete packet;

        int nSent = 0;
        getIntegerParam(TxCount, &nSent);
        setIntegerParam(TxCount, ++nSent);

        // Invalidate all params
        setIntegerParam(RspCmd,     0);
        setIntegerParam(RspCmdAck,  0);
        setIntegerParam(RspHwType,  0);
        setStringParam(RspSrc,      "");
        setStringParam(RspRouter,   "");
        setStringParam(RspDest,     "");
        setIntegerParam(RspLen,     0);
        setIntegerParam(RspDataLen, 0);
        m_payloadLen = 0;

        callParamCallbacks();
    }
}

void GenericModulePlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    getIntegerParam(RxCount,    &nReceived);
    getIntegerParam(ProcCount,  &nProcessed);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        // Silently skip packets we're not interested in
        if (!packet->isResponse() || packet->getSourceAddress() != m_hardwareId)
            continue;

        if (response(packet))
            nProcessed++;
    }

    setIntegerParam(RxCount,    nReceived);
    setIntegerParam(ProcCount,  nProcessed);
    callParamCallbacks();
}

bool GenericModulePlugin::response(const DasPacket *packet)
{
    setIntegerParam(RspCmd,     packet->getResponseType());
    setIntegerParam(RspCmdAck,  (packet->cmdinfo.command == DasPacket::RSP_NACK || packet->cmdinfo.command == DasPacket::RSP_ACK) ? packet->cmdinfo.command : 0);
    setIntegerParam(RspHwType,  static_cast<int>(packet->cmdinfo.module_type));
    setStringParam(RspSrc,      BaseModulePlugin::addr2ip(packet->getSourceAddress()).c_str());
    setStringParam(RspRouter,   BaseModulePlugin::addr2ip(packet->getRouterAddress()).c_str());
    setStringParam(RspDest,     BaseModulePlugin::addr2ip(packet->destination).c_str());
    setIntegerParam(RspLen,     sizeof(DasPacket) + packet->payload_length);
    setIntegerParam(RspDataLen, packet->getPayloadLength());

    // Cache the payload to read it through readOctet()
    m_payloadLen = std::min(packet->getPayloadLength()/4, static_cast<uint32_t>(sizeof(m_payload)));
    memcpy(m_payload, packet->getPayload(), m_payloadLen*4);

    return true;
}
