#ifndef ADARA_PLUGIN_H
#define ADARA_PLUGIN_H

#include "BaseSocketPlugin.h"

/**
 * Plugin that forwards Neutron Event data to ADARA SMS over TCP/IP.
 *
 * The plugin registers to receive all packets, but will only process and
 * forward neutron event data packets which have RTDL header attached and
 * the RTDL packets.
 *
 * After setting at least LISTEN_ADDR parameter, the plugin will start listening
 * for incoming TCP/IP connections. LISTEN_ADDR and LISTEN_PORT can be changed
 * at any time, which will reconfigure listening socket and also disconnect
 * ADARA SMS client, if connected.
 *
 * When enabled, processing of packets will only occur if there's a client
 * which accepts data. In this case, the Neutron Event data packets and RTDL
 * packets are transformed into ADARA format and sent over socket, one packet
 * at a time. Client socket is disconnected on any error. ADARA is supposed
 * to reconnect immediately.
 *
 * AdaraPlugin provides following asyn parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 */
class AdaraPlugin : public BaseSocketPlugin {
    private:
        uint64_t m_nTransmitted;    //!< Number of packets sent to BASESOCKET
        uint64_t m_nProcessed;      //!< Number of processed packets
        uint64_t m_nReceived;       //!< Number of packets received from dispatcher

    public:
        /**
         * Constructor
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] blocking Should processing of callbacks block execution of caller thread or not.
         */
        AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking);

        /**
         * Destructor
         */
        ~AdaraPlugin();

        /**
         * Process RTDL and NeutronData packets only, skip rest.
         *
         * @param[in] packetList List of packets to be processed
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Overloaded periodic function to send ADARA Heartbeat packet.
         *
         * Send Heartbeat packet and call base BaseSocketPlugin::checkClient()
         * function for timer upkeep.
         *
         * @return Number returned from BaseSocketPlugin::checkClient()
         */
        float checkClient();
};

#endif // ADARA_PLUGIN_H
