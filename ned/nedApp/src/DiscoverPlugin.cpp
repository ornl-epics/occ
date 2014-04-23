#include "DiscoverPlugin.h"
#include "Log.h"

#include <string.h>

#define NUM_DISCOVERPLUGIN_PARAMS ((int)(&LAST_DISCOVERPLUGIN_PARAM - &FIRST_DISCOVERPLUGIN_PARAM + 1))

EPICS_REGISTER_PLUGIN(DiscoverPlugin, 2, "Port name", string, "Dispatcher port name", string);

DiscoverPlugin::DiscoverPlugin(const char *portName, const char *dispatcherPortName)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, 1, NUM_DISCOVERPLUGIN_PARAMS)
{
    createParam("TRIGGER",              asynParamInt32, &Trigger);
    createParam("DISCOVERED_TOTAL",     asynParamInt32, &DiscoveredTotal);
    createParam("DISCOVERED_DSPS",      asynParamInt32, &DiscoveredDsps);

    setIntegerParam(DiscoveredTotal, 0);
    setIntegerParam(DiscoveredDsps,  0);

    setCallbacks(true);

    bool parity;
    parity = evenParity(0xA);
    parity = evenParity(0xB);

    callParamCallbacks();
}

asynStatus DiscoverPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == Trigger) {
        m_discovered.clear();
        reqDiscover();
        return asynSuccess;
    }
    return BasePlugin::writeInt32(pasynUser, value);
}

void DiscoverPlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    int nDiscoveredTotal = 0;
    getIntegerParam(ReceivedCount,  &nReceived);
    getIntegerParam(ProcessedCount, &nProcessed);
    getIntegerParam(DiscoveredTotal,&nDiscoveredTotal);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        // Silently skip packets we're not interested in
        if (!packet->isResponse() || packet->cmdinfo.command != DasPacket::CMD_DISCOVER)
            continue;

        if (packet->cmdinfo.module_type == DasPacket::MOD_TYPE_DSP) {
            // DSP responds with a list of modules it knows about in the payload.
            // It appears that only DSPs will respond to a broadcast address and from
            // their responses all their submodules can be observed. Since we're
            // also interested in module types, we'll do a p2p discover to every module.
            m_discovered[packet->source].type = packet->cmdinfo.module_type;
            for (int i=0; i<packet->payload_length/sizeof(uint32_t); i++) {
                reqLvdsDiscover(packet->payload[i]);
            }
        } else if (packet->cmdinfo.is_passthru) {
            // Source hardware id belongs to the DSP, the actual module id is in payload
            m_discovered[packet->payload[0]].type = packet->cmdinfo.module_type;
            m_discovered[packet->payload[0]].parent = packet->source;
        } else {
            m_discovered[packet->source].type = packet->cmdinfo.module_type;
        }

        int val;
        getIntegerParam(DiscoveredDsps, &val);
        setIntegerParam(DiscoveredDsps, val + 1);

        nDiscoveredTotal++;
        nProcessed++;
    }

    setIntegerParam(DiscoveredTotal,nDiscoveredTotal);
    setIntegerParam(ReceivedCount,  nReceived);
    setIntegerParam(ProcessedCount, nProcessed);
    callParamCallbacks();
}

void DiscoverPlugin::report(FILE *fp, int details)
{
    fprintf(fp, "Discovered modules:\n");
    for (std::map<uint32_t, ModuleDesc>::iterator it = m_discovered.begin(); it != m_discovered.end(); it++) {
        char moduleId[16];
        char parentId[16];
        char *type;

        switch (it->second.type) {
            case DasPacket::MOD_TYPE_ACPC:      type = "ACPC";      break;
            case DasPacket::MOD_TYPE_ACPCFEM:   type = "ACPC FEM";  break;
            case DasPacket::MOD_TYPE_AROC:      type = "AROC";      break;
            case DasPacket::MOD_TYPE_BIDIMROC:  type = "BIDIMROC";  break;
            case DasPacket::MOD_TYPE_BLNROC:    type = "BLNROC";    break;
            case DasPacket::MOD_TYPE_CROC:      type = "CROC";      break;
            case DasPacket::MOD_TYPE_DSP:       type = "DSP";       break;
            case DasPacket::MOD_TYPE_FFC:       type = "FFC";       break;
            case DasPacket::MOD_TYPE_FEM:       type = "FEM";       break;
            case DasPacket::MOD_TYPE_HROC:      type = "HROC";      break;
            case DasPacket::MOD_TYPE_IROC:      type = "IROC";      break;
            case DasPacket::MOD_TYPE_ROC:       type = "ROC";       break;
            case DasPacket::MOD_TYPE_SANSROC:   type = "SANSROC";   break;
            default:                            type = "unknown module type";
        }

        resolveIP(it->first, moduleId);
        resolveIP(it->second.parent, parentId);
        if (it->second.parent != 0)
            fprintf(fp, "\t%-8s: %15s (DSP=%s)\n", type, moduleId, parentId);
        else
            fprintf(fp, "\t%-8s: %15s\n", type, moduleId);
    }
    return BasePlugin::report(fp, details);
}

void DiscoverPlugin::reqDiscover()
{
    DasPacket *packet = DasPacket::create(0, 0);
    packet->source = DasPacket::HWID_SELF;
    packet->destination = DasPacket::HWID_BROADCAST;
    packet->cmdinfo.is_command = true;
    packet->cmdinfo.command = DasPacket::CMD_DISCOVER;

    sendToDispatcher(packet);
    delete packet;
}

void DiscoverPlugin::reqLvdsDiscover(uint32_t hardwareId)
{
    DasPacket *packet = DasPacket::create(2*sizeof(uint32_t), 0);
    packet->source = DasPacket::HWID_SELF;
    packet->destination = 0;
    packet->cmdinfo.is_command = true;
    packet->cmdinfo.is_passthru = true;
    packet->cmdinfo.lvds_all_one = 0x3;
    packet->cmdinfo.lvds_global = (hardwareId == DasPacket::HWID_BROADCAST);
    packet->cmdinfo.lvds_parity = evenParity(DasPacket::CMD_DISCOVER);
    packet->cmdinfo.command = DasPacket::CMD_DISCOVER;
    if (hardwareId != DasPacket::HWID_BROADCAST) {
        packet->payload[0] = hardwareId & 0xFFFF;
        packet->payload[0] |= (evenParity(packet->payload[0]) << 16);
        packet->payload[1] = (hardwareId >> 16 ) & 0xFFFF;
        packet->payload[1] |= (evenParity(packet->payload[1]) << 16); // last bit changes parity
        packet->payload[1] |= (0x1 << 17);
    } else {
        packet->payload_length = 0;
    }

    sendToDispatcher(packet);
    delete packet;
}

bool DiscoverPlugin::evenParity(int number)
{
    int temp = 0;
    while (number != 0) {
        temp = temp ^ (number & 0x1);
        number >>= 1;
    }
    return temp;
}

void DiscoverPlugin::resolveIP(uint32_t hardwareId, char *ip)
{
    uint8_t tokens[4];
    for (int i=0; i<4; i++) {
        tokens[i] = (hardwareId >> (i*8)) & 0xFF;
    }
    sprintf(ip, "%u.%u.%u.%u", tokens[3], tokens[2], tokens[1], tokens[0]);
}
