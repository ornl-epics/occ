#ifndef GENERIC_MODULE_PLUGIN_H
#define GENERIC_MODULE_PLUGIN_H

#include "BaseModulePlugin.h"

#include <cstring>

/**
 * Generic module plugin is a tool to test and debug module communication.
 *
 * The plugin is mainly useful for testing. User must first select remote
 * module and command. Writing command triggers sending the packet out and
 * waiting for response. Incoming response from the configured module will
 * immediately populate all PVs.
 *
 * Plugin requires a valid hardware address of the remote module. It does not
 * send out global commands. Format of outgoing packets depends on the link
 * type. Only DSPs are connected through optics, all other modules are
 * connected to DSP through LVDS. When sending a packet through LVDS, extra
 * formatting is necessary. User must select what type of communication to use
 * through ReqIsDsp parameter.
 *
 * User can send any 8 bit OCC command. The plugin does not check command value
 * and thus allows extensibility through EPICS database.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 * ReqDest       | asynParamOctet  | ""       | RW   | Module address to communicate with
 * ReqCmd        | asynParamInt32  | 0        | RW   | Command to be sent
 * ReqIsDsp      | asynParamInt32  | 0        | RW   | Is the module we communicate with behinds the DSP, using LVDS link
 * RspCmd        | asynParamInt32  | 0        | RO   | Response command, see DasPacket::CommandType
 * RspCmdAck     | asynParamInt32  | 0        | RO   | Response ACK/NACK
 * RspHwType     | asynParamInt32  | 0        | RO   | Hardware type, see DasPacket::ModuleType
 * RspSrc        | asynParamOctet  | 0        | RO   | Response source address
 * RspRouter     | asynParamOctet  | 0        | RO   | Response router address
 * RspDest       | asynParamOctet  | 0        | RO   | Response destination address
 * RspLen        | asynParamInt32  | 0        | RO   | Response length in bytes
 * RspDataLen    | asynParamInt32  | 0        | RO   | Response payload length in bytes
 * RspData       | asynParamInt32  | 0        | RO   | Response payload
 */
class GenericModulePlugin : public BasePlugin {
    private: // variables
        static const int defaultInterfaceMask = BasePlugin::defaultInterfaceMask | asynOctetMask;
        static const int defaultInterruptMask = BasePlugin::defaultInterruptMask | asynOctetMask;

        uint32_t m_hardwareId;      //!< Currently set module address
        uint32_t m_payload[256];    //!< Last packet payload
        uint32_t m_payloadLen;      //!< Last packet payload length, in number of elements in m_payload

    public: // structures and defines
        /**
         * Constructor for GenericModulePlugin
         *
         * Constructor will create and populate PVs with default values.
         *
         * @param[in] portName asyn port name.
         * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        GenericModulePlugin(const char *portName, const char *dispatcherPortName, int blocking=0);

        /**
         * Overloaded function to handle writing strings and byte arrays.
         */
        asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);

        /**
         * Overloaded function to handle reading strings and byte arrays.
         */
        asynStatus readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason);

        /**
         * Overloaded function to handle writing integers.
         */
        virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Overloaded function to process incoming OCC packets.
         */
        virtual void processData(const DasPacketList * const packetList);

    private:
        /**
         * Send a command request to the currently selected module.
         */
        void request(const DasPacket::CommandType command);

        /**
         * Process command response from currently selected module.
         */
        bool response(const DasPacket *packet);

    protected:
        #define FIRST_GENERICMODULEPLUGIN_PARAM ReqDest
        int ReqDest;        //!< Module address to communicate with
        int ReqCmd;         //!< Command to be sent
        int ReqIsDsp;       //!< Is the module we communicate with behinds the DSP, using LVDS link
        int RspCmd;         //!< Response command, see DasPacket::CommandType
        int RspCmdAck;      //!< Response ACK/NACK
        int RspHwType;      //!< Hardware type, see DasPacket::ModuleType
        int RspSrc;         //!< Response source address
        int RspRouter;      //!< Response router address
        int RspDest;        //!< Response destination address
        int RspLen;         //!< Response length in bytes
        int RspDataLen;     //!< Response payload length in bytes
        int RspData;        //!< Response payload
        #define LAST_GENERICMODULEPLUGIN_PARAM RspData
};

#endif // GENERIC_MODULE_PLUGIN_H
