#ifndef ADARA_PLUGIN_H
#define ADARA_PLUGIN_H

#include "BasePlugin.h"

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
 * asyn param name      | asyn param index | asyn param type | init val | mode | Description
 * -------------------- | ---------------- | --------------- | -------- | ---- | -----------
 * LISTEN_IP            | ListenIP         | asynParamOctet  | <empty>  | RW   | Hostname or IP address to listen to
 * LISTEN_PORT          | ListenPort       | asynParamInt32  | 5656     | RW   | Port number to listen to
 * CLIENT_IP            | ClientIP         | asynParamOctet  | <empty>  | RO   | IP of ADARA client if connected, or empty string
 * TRANSMITTED_COUNT    | TransmittedCount | asynParamInt32  | 0        | RO   | Number of packets sent to ADARA
 */

class AdaraPlugin : public BasePlugin {
    public:
        /**
         * Constructor
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] blocking Should processing of callbacks block execution of caller thread or not.
         */
        AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking=0);

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
         * Handle AdaraPlugin integer parameters changes.
         */
        asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Handle AdaraPlugin string parameter changes.
         */
        asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);

    private:
        #define FIRST_ADARAPLUGIN_PARAM ListenIP
        int ListenIP;
        int ListenPort;
        int ClientIP;
        int TransmittedCount;
        #define LAST_ADARAPLUGIN_PARAM TransmittedCount

    private:
        int m_listenSock;           //!< Socket for incoming connections, -1 when not listening
        int m_clientSock;           //!< Client socket to send/receive data to/from, -1 when no client
        int *m_outpacket;           //!< Buffer for output packet
        uint64_t m_nTransmitted;    //!< Number of packets sent to ADARA
        uint64_t m_nProcessed;      //!< Number of processed packets
        uint64_t m_nReceived;       //!< Number of packets received from dispatcher

        /**
         * Setup listening socket.
         *
         * The old listening socket is closed only if new one is successfully created.
         *
         * @param[in] host Hostname or IP address.
         * @param[in] port Port number to listen on.
         * @retunr true when configured, false otherwise.
         */
        bool setupListeningSocket(const std::string &host, uint16_t port);

        /**
         * Try to connect client socket, don't block.
         *
         * Caller must hold a lock. When returned with true, m_clientSock is a valid
         * client id. Function updates CLIENT_IP parameter.
         *
         * @param[out] clientHost When connected, dotted IP address of the client, followed by the port.
         * @return true if connected, false otherwise.
         */
        bool connectClient();

        /**
         * Disconnect client if connected.
         *
         * Caller must hold a lock. Function updates CLIENT_IP parameter.
         *
         * @return true when disconnected or no client, false on error.
         */
        void disconnectClient();

        /**
         * Send data to client through m_clientSock.
         *
         * If an error occurs while sending, m_clientSock might get disconnected
         * based on error severity. Rest of the class will take care of automatically
         * reconnect new incoming connection.
         *
         * Caller of this function must ensure locked access. Function will block until
         * all data has been sent to socket or error occurs.
         *
         * @param[in] data Data to be sent through the socket.
         * @param[in] length Length of data in bytes.
         * @return true if all data has been sent, false on error
         */
        bool send(const uint32_t *data, uint32_t length);
};

#endif // ADARA_PLUGIN_H
