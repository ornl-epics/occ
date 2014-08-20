#ifndef DISCOVER_PLUGIN_H
#define DISCOVER_PLUGIN_H

#include "BasePlugin.h"
#include "BaseModulePlugin.h"

#include <map>

/**
 * Module discovery plugin.
 *
 * The plugin will discover all modules and remember them internally. asyn report
 * can be used to print details of all discovered details.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * Trigger       | asynParamInt32  | 0        | RW   | Trigger discovery
 * Output        | asynParamOctet  | Not init | RO   | Output text listing found modules
 */
class DiscoverPlugin : public BasePlugin {

    private:
        /**
         * Module description.
         */
        struct ModuleDesc {
            DasPacket::ModuleType type;
            uint32_t parent;
            BaseModulePlugin::Version version;

            ModuleDesc()
                : type(static_cast<DasPacket::ModuleType>(0))
                , parent(0)
            {}
        };

        std::map<uint32_t, ModuleDesc> m_discovered;    //!< Map of modules discovered, key is module's hardware id
        static const int defaultInterruptMask = BasePlugin::defaultInterruptMask | asynOctetMask;

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
         * Overloaded method to handle reading the output string.
         */
        asynStatus readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason);

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
         * Send a READ_VERSION command to a particular DSP.
         */
        void reqVersion(uint32_t hardwareId);

        /**
         * Send a READ_VERSION command to a particular module through LVDS.
         */
        void reqLvdsVersion(uint32_t hardwareId);

        /**
         * Convert hardware id to IP address.
         *
         * @param[in] hardwareId Id to convert.
         * @param[out] ip Array to receive converted string, must be at least 16 chars long, it's always \0 terminated
         */
        void resolveIP(uint32_t hardwareId, char *ip);

        /**
         * Print discovered modules into a buffer
         */
        uint32_t formatOutput(char *buffer, uint32_t size);

    private:
        #define FIRST_DISCOVERPLUGIN_PARAM Trigger
        int Trigger;            //!< Trigger discovery of modules
        int Output;
        #define LAST_DISCOVERPLUGIN_PARAM Output
};

#endif // DISCOVER_PLUGIN_H
