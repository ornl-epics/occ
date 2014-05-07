#ifndef BASE_MODULE_PLUGIN_H
#define BASE_MODULE_PLUGIN_H

#include "BasePlugin.h"
#include "StateMachine.h"

#include <map>

/**
 * Abstract module plugin.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 * HwId          | asynParamInt32  | 0        | RO   | Connected module hardware id
 * Status        | asynParamInt32  | 0        | RO   | Status of RocPlugin            (0=not initialized,1=response timeout,2=invalid type,3=version mismatch,12=ready)
 * Command       | asynParamInt32  | 0        | RW   | Issue RocPlugin command        (1=initialize,2=read status,3=write config to module,4=read config from module)
 */
class BaseModulePlugin : public BasePlugin {
    public: // structures and defines
        /**
         * Valid commands to be send to module through OCC.
         */
        enum Command {
            CMD_NONE                = 0,
            CMD_INITIALIZE          = 1,    //!< Trigger RO module initialization
            CMD_READ_STATUS         = 2,    //!< Trigger reading status from module
            CMD_WRITE_CONFIG        = 3,    //!< Write current configuration to module
            CMD_READ_CONFIG         = 4,    //!< Read actual configuration from module and populate PVs accordingly
        };

        /**
         * Valid statuses of the plugin.
         */
        enum Status {
            STAT_NOT_INITIALIZED    = 0,    //!< RocPlugin has not yet been initialized
            STAT_TIMEOUT            = 1,    //!< Module is not responding to commands
            STAT_TYPE_MISMATCH      = 2,    //!< Module with given address is not ROC board
            STAT_VERSION_MISMATCH   = 3,    //!< Actual module version does not match configured one
            STAT_TYPE_VERIFIED      = 10,   //!< Module type has been verified to be ROC board
            STAT_VERSION_VERIFIED   = 11,   //!< Module version has been verified to match configured one
            STAT_READY              = 12,   //!< Module is ready
        };

        /**
         * Valid state machine actions.
         */
        enum Action {
            DISCOVER_OK,                    //!< Received valid DISCOVER response which matches ROC type
            DISCOVER_MISMATCH,              //!< Received invalid DISCOVER response which doesn't match ROC type
            VERSION_READ_OK,                //!< Received valid READ_STATUS response which matches configured version
            VERSION_READ_MISMATCH,          //!< Received invalid READ_STATUS response which doesn't match configured version
            TIMEOUT,                        //!< Timeout occurred
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

        /**
         * Structure describing the config parameters obtained from modules.
         */
        struct ConfigParamDesc {
            char section;           //!< Section name
            uint32_t offset;        //!< An 4-byte offset within the section
            uint32_t shift;         //!< Position of the field bits within 32 bits dword at given offset
            uint32_t width;         //!< Number of bits used for the value
            int initVal;            //!< Initial value after object is created or configuration reset is being requested
        };

    protected: // variables
        uint32_t m_hardwareId;                          //!< Hardware ID which this plugin is connected to
        uint32_t m_statusPayloadLength;                 //!< Size in bytes of the READ_STATUS request/response payload, calculated dynamically by createStatusParam()
        uint32_t m_configPayloadLength;                 //!< Size in bytes of the READ_CONFIG request/response payload, calculated dynamically by createConfigParam()
        std::map<int, StatusParamDesc> m_statusParams;  //!< Map of exported status parameters
        std::map<int, ConfigParamDesc> m_configParams;  //!< Map of exported config parameters
        StateMachine<enum Status, int> m_stateMachine;  //!< State machine for the current status

    private: // variables
        ConnectionType m_connType;
        std::map<char, uint32_t> m_configSectionSizes;
        std::map<char, uint32_t> m_configSectionOffsets;

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
         * Destructor
         */
        virtual ~BaseModulePlugin();

        /**
         * Handle parameters write requests for integer type.
         *
         * When an integer parameter is written through PV, this function
         * gets called with a new value. It handles the Command parameter
         * as well as all configuration parameters.
         *
         * @param[in] pasynUser asyn handle
         * @param[in] value New value to be applied
         * @return asynSuccess on success, asynError otherwise
         */
        virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

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
         * Build WRITE_CONFIG payload and send it to module.
         *
         * Configuration data is gathered from configuration parameters
         * and their current values. Configuration packet is created with
         * configuration data attached and sent to OCC, but not waited for
         * response.
         * Access to this function should be locked before calling it or
         * otherwise configuration data inconsistencies can occur. Luckily,
         * this function gets triggered from parameter update, which is
         * already locked.
         *
         * This function is asynchronous but does not wait for response.
         */
        void reqConfigWrite();

        /**
         * Overloaded incoming data handler.
         *
         * Function iterates through receives packets and silently skipping
         * the ones that their destination does not match connected module
         * or the packet is not response to a command.
         * Packets to be processed are handed to processResponse() function
         * for real work.
         * Function also updates statistical parameters like number of received
         * and processed packets.
         *
         * @param[in] packets List of packets received in this batch.
         */
        virtual void processData(const DasPacketList * const packets);

        /**
         * General response from modules handler.
         *
         * This generic response handler recognizes all well-known responses and calls corresponding
         * handlers.
         *
         * @param[in] packet to be processed.
         * @return true if packet has been processed, false otherwise
         */
        virtual bool processResponse(const DasPacket *packet);

        /**
         * Abstract handler for DISCOVER response.
         *
         * @param[in] packet with response to DISCOVER
         * @return true if packet was parsed and type of module is ROC.
         */
        virtual bool rspDiscover(const DasPacket *packet) = 0;

        /**
         * Abstract handler for READ_VERSION response.
         *
         * @param[in] packet with response to READ_VERSION
         * @return true if packet was parsed and module version verified.
         */
        virtual bool rspReadVersion(const DasPacket *packet) = 0;

        /**
         * Default handler for READ_CONFIG response.
         *
         * Read the packet payload and populate status parameters.
         *
         * @param[in] packet with response to READ_STATUS
         * @return true if packet was parsed and module version verified.
         */
        virtual bool rspReadConfig(const DasPacket *packet);

        /**
         * Default handler for READ_STATUS response.
         *
         * Read the packet payload and populate status parameters.
         *
         * @param[in] packet with response to READ_STATUS
         * @return true if packet was parsed and module version verified.
         */
        virtual bool rspReadStatus(const DasPacket *packet);

        /**
         * Create and register single integer status parameter.
         */
        void createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift);

        /**
         * Create and register single integer config parameter.
         */
        void createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value);

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

        /**
         * Parse hardware id in IP like format or HEX prefixed with 0x.
         *
         * Recognized formats:
         * - "21.250.118.223"
         * - "0x15FA76DF"
         *
         * @param[in] text to be parsed
         * @return Parsed hardware ID or 0 on error.
         */
        static uint32_t parseHardwareId(const std::string &text);

    private: // functions
        /**
         * Trigger calculating the configuration parameter offsets.
         */
        void recalculateConfigParams();

    protected:
        #define FIRST_BASEMODULEPLUGIN_PARAM Command
        int Command;        //!< Command to plugin, like initialize the module, read configuration, verify module version etc.
        int Status;         //!< Status of the DSP plugin
    private:
        int HardwareId;     //!< Hardare ID that this object is controlling
        #define LAST_BASEMODULEPLUGIN_PARAM HardwareId

};

#endif // BASE_MODULE_PLUGIN_H
