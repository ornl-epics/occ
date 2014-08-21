#ifndef BASE_MODULE_PLUGIN_H
#define BASE_MODULE_PLUGIN_H

#include "BasePlugin.h"
#include "StateMachine.h"
#include "Timer.h"

#include <map>

#define SM_ACTION_CMD(a)        (a)
#define SM_ACTION_ACK(a)        ((a) | (0x1 << 16))
#define SM_ACTION_ERR(a)        ((a) | (0x1 << 17))
#define SM_ACTION_TIMEOUT(a)    ((a) | (0x1 << 18))

/**
 * Base class for all plugins working with particular module.
 *
 * The BaseModulePlugin provides functionality common to all modules
 * handler plugins. It's able to construct outgoing packet with all
 * the raw fields. It filters out incoming packets which don't originate
 * from the connected module and invokes appropriate response handler
 * for each response, which can be overloaded by derived classes.
 * It also hides the complexity of connection type and simplifies it
 * to a single "is module behind the DSP" question, which is discussed
 * in details in next section.
 *
 * Submodules connected through DSP use LVDS connection and their
 * responses contain 16-bit data fields packet into 24-bit LVDS
 * words. Extra 8-bits are used by the LVDS channel for parity,
 * start/stop bits etc. DSP on the other hand sends 32-bit data
 * fields and the CRC at the end of the packet checks the correctness
 * of all the data. When an LVDS packet is passing the DSP, DSP
 * might transform it. When sending a packet to the module behind
 * the DSP, DSP expects to received LVDS packet tagged as pass-thru.
 * OCC header destination is not used, instead the destination address
 * must be included as first two dwords in the packet's payload.
 * The actual payload follows in 16-bit words. Each dword in the
 * pass-thru OCC packet must have upper 8 bits cleared, and the LVDS
 * control bits must be calculated.
 * Receiving response from a module behind the DSP is different.
 * DSP transforms the response from submodules. It takes away
 * LVDS control bits and joins 2 LVDS 16-bit data fields into
 * single 32-bit dword field - first LVDS word in lower-part of dword
 * and second LVDS word in upper-part of dword
 *
 * BaseModulePlugin supports all common module communication, including
 * DISCOVER, READ_VERSION, READ_STATUS, READ_CONFIG, WRITE_CONFIG,
 * START, STOP. There's 2 overloadable handlers for each type, reqXY
 * sends a command to module and rspXY processes the response to that
 * command. Sending out a command recharges the timer which defaults
 * to 2 seconds. Received response cancels the timeout. No other command
 * can be issues while waiting for response.
 *
 * There's a generic status and configuration parameter description
 * functionality that all modules should use. Modules should only
 * create a table of parameters using createStatusParam() and
 * createConfigParam() functions. The BaseModulePlugin will make
 * sure to properly map those into OCC packets.
 *
 * Commands can be issued independently from each other as long as
 * response for the previous command was received or timed out.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 * HwId          | asynParamInt32  | 0        | RO   | Connected module hardware id
 * LastCmdRsp    | asynParamInt32  | 0        | RO   | Last command response status   (see LastCommandResponse for valid values)
 * Command       | asynParamInt32  | 0        | RW   | Issue RocPlugin command        (see DasPacket::CommandType for valid values)
 * Supported     | asynParamInt32  | 0        | RO   | Flag whether module is supported
 * Verified      | asynParamInt32  | 0        | RO   | Flag whether module type and version were verified
 * Type          | asynParamInt32  | 0        | RO   | Module type                    (see DasPacket::ModuleType for valid values)
 */
class BaseModulePlugin : public BasePlugin {
    public: // structures and defines
        /**
         * Valid last command return codes
         */
        enum LastCommandResponse {
            LAST_CMD_NONE           = 0,    //!< No command issued yet
            LAST_CMD_OK             = 1,    //!< Last command response received and parsed
            LAST_CMD_WAIT           = 2,    //!< Waiting for last command response
            LAST_CMD_TIMEOUT        = 3,    //!< Did not receive response for the last command
            LAST_CMD_ERROR          = 4,    //!< Error processing last command response
        };

        /**
         * Possible module verification statuses
         */
        enum TypeVersionStatus {
            ST_TYPE_VERSION_INIT    = 0,
            ST_TYPE_OK              = 1,
            ST_TYPE_ERR             = 2,
            ST_VERSION_OK           = 3,
            ST_VERSION_ERR          = 4,
            ST_TYPE_VERSION_OK      = 5,
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

        struct Version {
            uint8_t hw_version;
            uint8_t hw_revision;
            uint16_t hw_year;
            uint8_t hw_month;
            uint8_t hw_day;
            uint8_t fw_version;
            uint8_t fw_revision;
            uint16_t fw_year;
            uint8_t fw_month;
            uint8_t fw_day;

            Version()
                : hw_version(0)
                , hw_revision(0)
                , hw_year(0)
                , hw_month(0)
                , hw_day(0)
                , fw_version(0)
                , fw_revision(0)
                , fw_year(0)
                , fw_month(0)
                , fw_day(0)
            {}
        };

        static const float NO_RESPONSE_TIMEOUT;         //!< Number of seconds to wait for module response

    public: // variables
        static const int defaultInterfaceMask = BasePlugin::defaultInterfaceMask | asynOctetMask;
        static const int defaultInterruptMask = BasePlugin::defaultInterruptMask | asynOctetMask;

    protected: // variables
        uint32_t m_hardwareId;                          //!< Hardware ID which this plugin is connected to
        uint32_t m_statusPayloadLength;                 //!< Size in bytes of the READ_STATUS request/response payload, calculated dynamically by createStatusParam()
        uint32_t m_configPayloadLength;                 //!< Size in bytes of the READ_CONFIG request/response payload, calculated dynamically by createConfigParam()
        std::map<int, StatusParamDesc> m_statusParams;  //!< Map of exported status parameters
        std::map<int, ConfigParamDesc> m_configParams;  //!< Map of exported config parameters
        StateMachine<TypeVersionStatus, int> m_verifySM;//!< State machine for verification status
        DasPacket::CommandType m_waitingResponse;       //!< Expected response code while waiting for response or timeout event, 0 otherwise

    private: // variables
        bool m_behindDsp;
        std::map<char, uint32_t> m_configSectionSizes;  //!< Configuration section sizes, in words (word=2B for submodules, =4B for DSPs)
        std::map<char, uint32_t> m_configSectionOffsets;//!< Status response payload size, in words (word=2B for submodules, =4B for DSPs)
        std::shared_ptr<Timer> m_timeoutTimer;          //!< Currently running timer for response timeout handling

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
         * @param[in] behindDsp Is this module behind the DSP which transforms some of the packets?
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         * @param[in] numParams The number of parameters that the derived class supports.
         */
        BaseModulePlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId,
                         bool behindDsp, int blocking=0, int numParams=0,
                         int interfaceMask=BaseModulePlugin::defaultInterfaceMask,
                         int interruptMask=BaseModulePlugin::defaultInterruptMask);

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
         * Note: The length parameter is in bytes, although the payload is an array of
         * 4-byte unsigned integers. OCC packets are always 4 byte aligned, but the LVDS
         * data in them might be 2 byte aligned. The payload should always point to an
         * array of 4 byte unsigned integers. The length should be dividable by 2.
         *
         * @param[in] command A command of the packet to be sent out.
         * @param[in] packet Payload to be sent out, can be NULL if length is also 0.
         * @param[in] length Payload length in bytes.
         */
        void sendToDispatcher(DasPacket::CommandType command, uint32_t *payload=0, uint32_t length=0);

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
         * Called when discover request to the module should be made.
         *
         * Base implementation simply sends a DISCOVER command and sets up
         * timeout callback.
         */
        virtual void reqDiscover();

        /**
         * Default handler for DISCOVER response.
         *
         * Only check for timeout.
         *
         * @param[in] packet with response to DISCOVER
         * @return true if timeout has not yet expired, false otherwise.
         */
        virtual bool rspDiscover(const DasPacket *packet) = 0;

        /**
         * Called when read version request to the module should be made.
         *
         * Base implementation simply sends a READ_VERSION command and sets up
         * timeout callback.
         */
        virtual void reqReadVersion();

        /**
         * Default handler for READ_VERSION response.
         *
         * Only check for timeout.
         *
         * @param[in] packet with response to READ_VERSION
         * @return true if timeout has not yet expired, false otherwise.
         */
        virtual bool rspReadVersion(const DasPacket *packet);

        /**
         * Called when read status request to the module should be made.
         *
         * Base implementation simply sends a READ_STATUS command and sets up
         * timeout callback.
         */
        virtual void reqReadStatus();

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
         * Called when read config request to the module should be made.
         *
         * Base implementation simply sends a READ_CONFIG command and sets up
         * timeout callback.
         */
        virtual void reqReadConfig();

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
         * Construct WRITE_CONFIG payload and send it to module.
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
         * This function is asynchronous and does not wait for response.
         */
        virtual void reqWriteConfig();

        /**
         * Default handler for READ_CONFIG response.
         *
         * Default implementation checks whether timeout callbacks has already
         * kicked in and cancels still pending timeout timer.
         *
         * @param[in] packet with response to READ_STATUS
         * @retval true Timeout has not yet occurred
         * @retval false Timeout has occurred and response is invalid.
         */
        virtual bool rspWriteConfig(const DasPacket *packet);

        /**
         * Send START command to module.
         *
         * This function is asynchronous and does not wait for response.
         */
        virtual void reqStart();

        /**
         * Default handler for START response.
         *
         * @param[in] packet with response to START
         * @retval true Timeout has not yet occurred
         * @retval false Timeout has occurred and response is invalid.
         */
        virtual bool rspStart(const DasPacket *packet);

        /**
         * Send STOP command to module.
         *
         * This function is asynchronous and does not wait for response.
         */
        virtual void reqStop();

        /**
         * Default handler for STOP response.
         *
         * @param[in] packet with response to STOP
         * @retval true Timeout has not yet occurred
         * @retval false Timeout has occurred and response is invalid.
         */
        virtual bool rspStop(const DasPacket *packet);

        /**
         * Create and register single integer status parameter.
         *
         * Status parameter is an individual status entity exported by module.
         * It can be a flag that some event occured or it can be a value like
         * number of errors. The createStatusParam() function covers them all.
         * Parameters don't exceed 32 bits. However, they can span
         * over 32bit boundary if they're shifted.
         * This function recognizes whether it's working with submodule and
         * calculates the real offset in the response. The offset parameter
         * should thus be specified in format used by the module (word 7 on LVDS
         * should be specified as offset 0x7 and dword 7 on DSP should also be
         * specified as 0x7).
         *
         * @param[in] name Parameter name must be unique within the plugin scope.
         * @param[in] offset word/dword offset within the payload.
         * @param[in] nBits Width of the parameter in number of bits.
         * @param[in] shift Starting bit position within the word/dword.
         */
        void createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift);

        /**
         * Create and register single integer config parameter.
         */
        void createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value);

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

        /**
         * Converter integer hardware ID into IP address.
         */
        void formatHardwareId(uint32_t hardwareId, std::string &ip);

        /**
         * A no-response cleanup function.
         *
         * The timeout is canceled if the response is received before timeout
         * expires.
         *
         * @param[in] command Command sent to module for which response should be received.
         * @return true if the timeout function did the cleanup, that is received was *not* received.
         */
        virtual float noResponseCleanup(DasPacket::CommandType command);

        /**
         * Request a custom callback function to be called at some time in the future.
         *
         * Uses BasePlugin::scheduleCallback() for scheduling the BaseModulePlugin::noResponseTimeout()
         * function and stores the timer as a member variable.
         *
         * Function expects the Plugin to be locked.
         *
         * @param[in] command Expected command response
         * @param[in] delay Delay from now when to invoke the function, in seconds.
         * @retval true if callback was scheduled
         * @retval false if callback was not scheduled
         */
        bool scheduleTimeoutCallback(DasPacket::CommandType command, double delay);

        /**
         * Cancel any pending timeout callback and release the timer.
         *
         * Function expects the Plugin to be locked.
         *
         * @retval true if future callback was canceled
         * @retval false if callback was already invoked and it hasn't been canceled
         */
        bool cancelTimeoutCallback();

    private: // functions
        /**
         * Trigger calculating the configuration parameter offsets.
         */
        void recalculateConfigParams();

    protected:
        #define FIRST_BASEMODULEPLUGIN_PARAM Command
        int Command;        //!< Command to plugin, like initialize the module, read configuration, verify module version etc.
        int LastCmdRsp;     //!< Last command response status
        int HardwareVer;    //!< Module hardware version
        int HardwareRev;    //!< Module hardware revision
        int HardwareDate;   //!< Module hardware date
        int FirmwareVer;    //!< Module firmware version
        int FirmwareRev;    //!< Module firmware revision
        int FirmwareDate;   //!< Module firmware date
        int Supported;      //!< Flag whether module is supported
        int Verified;       //!< Hardware id, version and type all verified
        int Type;           //!< Configured module type
    private:
        int HardwareId;     //!< Hardware ID that this object is controlling
        #define LAST_BASEMODULEPLUGIN_PARAM HardwareId

};

#endif // BASE_MODULE_PLUGIN_H
