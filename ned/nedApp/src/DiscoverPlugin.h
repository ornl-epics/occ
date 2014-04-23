#ifndef DISCOVER_PLUGIN_H
#define DISCOVER_PLUGIN_H

#include "BasePlugin.h"

#include <map>

/**
 * Module discovery plugin.
 *
 * The plugin will discover all modules and remember them internally. asyn report
 * can be used to print details of all discovered details.
 *
 * General plugin parameters:
 * asyn param name      | asyn param index | asyn param type | init val | mode | Description
 * -------------------- | ---------------- | --------------- | -------- | ---- | -----------
 * TRIGGER              | Trigger          | asynParamInt32  | 0        | RW   | Trigger discovery.
 * DISCOVERED_TOTAL     | DiscoveredTotal  | asynParamInt32  | 0        | RO   | Number of all modules discovered.
 * DISCOVERED_DSPS      | DiscoveredDsps   | asynParamInt32  | 0        | RO   | Number of DSP modules discovered.
 */
class DiscoverPlugin : public BasePlugin {

    private:
        /**
         * Module description.
         */
        struct ModuleDesc {
            DasPacket::ModuleType type;
            uint32_t parent;

            ModuleDesc()
                : type(static_cast<DasPacket::ModuleType>(0))
                , parent(0)
            {}
        };

        std::map<uint32_t, ModuleDesc> m_discovered;    //!< Map of modules discovered, key is module's hardware id

    public:
        /**
         * Constructor for DiscoverPlugin
         *
         * Constructor will create and populate PVs with default values.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         */
        DiscoverPlugin(const char *portName, const char *dispatcherPortName);

    private:
        /**
         * Overloaded function called by asynPortDriver when the PV should change value.
         */
        asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Process incoming packets.
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Overloaded function to print details about discovered devices.
         */
        void report(FILE *fp, int details);

        /**
         * Send out a broadcast DISCOVER command to all modules.
         */
        void reqDiscover();

        /**
         * Send a DISCOVER command to a particular module through LVDS on DSP.
         */
        void reqLvdsDiscover(uint32_t hardwareId);

        /**
         * Return true if number has even number of 1s, false otherwise.
         */
        bool evenParity(int number);

        /**
         * Convert hardware id to IP address.
         *
         * @param[in] hardwareId Id to convert.
         * @param[out] ip Array to receive converted string, must be at least 16 chars long, it's always \0 terminated
         */
        void resolveIP(uint32_t hardwareId, char *ip);

    private:
        #define FIRST_DISCOVERPLUGIN_PARAM Trigger
        int Trigger;            //!< Trigger discovery of modules
        int DiscoveredTotal;    //!< Number of all modules discovered
        int DiscoveredDsps;     //!< Number of DSP modules discovered
        #define LAST_DISCOVERPLUGIN_PARAM DiscoveredDsps
};

#endif // DISCOVER_PLUGIN_H
