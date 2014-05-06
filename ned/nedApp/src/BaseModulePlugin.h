#ifndef BASE_MODULE_PLUGIN_H
#define BASE_MODULE_PLUGIN_H

#include "BasePlugin.h"

#include <map>

/**
 * Abstract module plugin.
 *
 * General plugin parameters:
 * asyn param name      | asyn param index | asyn param type | init val | mode | Description
 * -------------------- | ---------------- | --------------- | -------- | ---- | -----------
 * HwId                 | HardwareId       | asynParamInt32  | 0        | RO   | Hardware ID of the module connected to
 */
class BaseModulePlugin : public BasePlugin {
    public: // structures and defines
        /**
         * Valid commands to be send through COMMAND parameter.
         */
        enum Command {
            CMD_NONE                = 0,
            CMD_INITIALIZE          = 1,    //!< Trigger RO module initialization
            CMD_WRITE_CONFIG        = 2,    //!< Write current configuration to module
            CMD_READ_CONFIG         = 3,    //!< Read actual configuration from module and populate PVs accordingly
            CMD_RESET_CONFIG        = 4,    //!< Reset configuration to default values
            CMD_READ_STATUS         = 5,    //!< Trigger reading status from module
        };

        /**
         * Connection type for the module.
         *
         * DSP modules are connected to OCC directly through optics. DSP submodules
         * are connected to DSP through LVDS. In that case DSP passes thru almost
         * as-is. Optical packet and LVDS packets differ slightly.
         */
        enum ConnectionType {
            CONN_TYPE_OPTICAL,
            CONN_TYPE_LVDS,
        };

        /**
         * Structure describing the status parameters obtained from modules.
         */
        struct StatusParamDesc {
            uint32_t offset;        //!< An 4-byte offset within the payload
            uint32_t shift;         //!< Position of the field bits within 32 bits dword at given offset
            uint32_t width;         //!< Number of bits used for the value
        };

    protected: // variables
        uint32_t m_hardwareId;
        uint32_t m_statusPayloadLength;                 //!< Size in bytes of the READ_STATUS request/response payload, calculated dynamically by createStatusParam()
        std::map<int, StatusParamDesc> m_statusParams;  //!< Map of exported status parameters

    private: // variables
        ConnectionType m_connType;

    public: // functions

        /**
         * Constructor for BaseModulePlugin
         *
         * Constructor will create and populate PVs with default values.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] hardwareId Hardware ID of the module, can be in IP format (xxx.xxx.xxx.xxx) or
         *                       in hex number string in big-endian byte order (0x15FACB2D equals to IP 21.250.203.45)
         * @param[in] conn Type of connection this module is using. Depending on this parameter, outgoing packets are created differently.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         * @param[in] numParams The number of parameters that the derived class supports.
         */
        BaseModulePlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, ConnectionType conn, int blocking=0, int numParams=0);

        /**
         * Create a packet and send it through dispatcher to the OCC board
         *
         * Either optical or LVDS packet is created based on the connection type this
         * module uses. It then sends the packet to the dispatcher in order to be delivered
         * through OCC optical link to the module.
         *
         * @param[in] command A command of the packet to be sent out.
         * @param[in] packet Payload to be sent out, can be NULL if length is also 0.
         * @param[in] length Payload length in number of 4-bytes.
         */
        void sendToDispatcher(DasPacket::CommandType command, uint32_t *payload=0, uint32_t length=0);

        /**
         * Create and register single integer status parameter.
         */
        virtual void createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift);

        /**
         * Create an optical packet to be sent to DSP.
         *
         * @param[in] destination Hardware id of a module to receive command, can be one of DasPacket::HardwareId
         * @param[in] command Command to be put into the packet
         * @param[in] payload Payload to be sent as part of the packet.
         * @param[in] length Payload length in number of 4-bytes.
         * @return Newly created DasPacket pointer which must be deleted when not used anymore.
         */
        static DasPacket *createOpticalPacket(uint32_t destination, DasPacket::CommandType command, uint32_t *payload=0, uint32_t length=0);

        /**
         * Create a LVDS packet to be sent from DSP to their submodules.
         *
         * @param[in] destination Hardware id of a module to receive command, can be one of DasPacket::HardwareId
         * @param[in] command Command to be put into the packet
         * @param[in] payload Payload to be sent as part of the packet.
         * @param[in] length Payload length in number of 4-bytes.
         * @return Newly created DasPacket pointer which must be deleted when not used anymore.
         */
        static DasPacket *createLvdsPacket(uint32_t destination, DasPacket::CommandType command, uint32_t *payload=0, uint32_t length=0);

        /**
         * Return true if number has even number of 1s, false otherwise.
         */
        static bool evenParity(int number);

    protected:
        virtual void rspReadStatus(const DasPacket *packet);

    private:
        #define FIRST_BASEMODULEPLUGIN_PARAM HardwareId
        int HardwareId;     //!< Hardare ID that this object is controlling
        #define LAST_BASEMODULEPLUGIN_PARAM HardwareId

};

#endif // BASE_MODULE_PLUGIN_H
