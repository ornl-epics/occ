#ifndef BASESOCKET_PLUGIN_H
#define BASESOCKET_PLUGIN_H

#include "BasePlugin.h"

/**
 * Plugin with server socket capabilities
 *
 * This abstract plugin provides server socket functionality. When listen IP and
 * port parameters are being set through asyn write handlers, plugin will
 * automatically open single listening socket. There's no thread running to
 * check when the client actually connects, instead derived class should
 * periodically call connectClient() function.
 *
 * BaseSocketPlugin provides following asyn parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 * ListenIp      | asynParamOctet  | <empty>  | RW   | Hostname or IP address to listen to
 * ListenPort    | asynParamInt32  | 0        | RW   | Port number to listen to
 * ClientIp      | asynParamOctet  | <empty>  | RO   | IP of BASESOCKET client if connected, or empty string
 * TxCount       | asynParamInt32  | 0        | RO   | Number of packets sent to BASESOCKET
 */

class BaseSocketPlugin : public BasePlugin {
    private:
        int m_listenSock;           //!< Socket for incoming connections, -1 when not listening
        int m_clientSock;           //!< Client socket to send/receive data to/from, -1 when no client

    public:
	    static const int defaultInterfaceMask = asynOctetMask | BasePlugin::defaultInterfaceMask;
	    static const int defaultInterruptMask = asynOctetMask | BasePlugin::defaultInterruptMask;

        /**
         * Constructor
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] blocking Should processing of callbacks block execution of caller thread or not.
         */
        BaseSocketPlugin(const char *portName, const char *dispatcherPortName, int blocking, int numParams=0,
                     int maxAddr=1, int interfaceMask=defaultInterfaceMask, int interruptMask=defaultInterruptMask,
                     int asynFlags=0, int autoConnect=1, int priority=0, int stackSize=0);

        /**
         * Destructor
         */
        virtual ~BaseSocketPlugin();

        /**
         * Handle BaseSocketPlugin integer parameters changes.
         */
        virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Handle BaseSocketPlugin string parameter changes.
         */
        virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);

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
         * Check whether there's a remote client connected to the server port.
         *
         * Function does not validate connection. If the client disconnected but
         * the socket is still valid, function might return true.
         *
         * @return true if connected.
         */
        bool isClientConnected();

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

    protected:
        #define FIRST_BASESOCKETPLUGIN_PARAM ListenIP
        int ListenIP;
        int ListenPort;
        int ClientIP;
        int TxCount;
        #define LAST_BASESOCKETPLUGIN_PARAM TxCount
};

#endif // BASESOCKET_PLUGIN_H
