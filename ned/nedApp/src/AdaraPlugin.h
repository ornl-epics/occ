#ifndef ADARA_PLUGIN_H
#define ADARA_PLUGIN_H

#include "BasePlugin.h"

class AdaraPlugin : public BasePlugin {
    public:
        /**
         * Constructor
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         */
        AdaraPlugin(const char *portName, const char *dispatcherPortName);

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
        int ListenIP;
        #define FIRST_ADARAPLUGIN_PARAM ListenIP
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
         * client id and the clientHost is filled with the client IP address and remote port.
         *
         * @param[out] clientHost When connected, dotted IP address of the client, followed by the port.
         */
        bool connectClient(std::string &clientHost);

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
