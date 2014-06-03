#ifndef DASPACKET_HPP
#define DASPACKET_HPP

#include <stdint.h>

/**
 * Class representing a DAS packet.
 *
 * Don't introduce any virtual functions here as this will break allocating
 * objects from OCC buffer.
 */
struct DasPacket
{
    public:
        /**
         * Structure representing single Neutron Event
         */
        struct NeutronEvent {
            uint32_t tof;
            uint32_t pixelid;
        };

        /**
         * Structure representing RTDL header in data packets, when rtdl_present bit is set.
         */
        struct RtdlHeader {
            uint32_t timestamp_low;
            uint32_t timestamp_high;
            uint32_t charge;
            uint32_t general_info;
            uint32_t tsync_width;
            uint32_t tsync_delay;
        };

        /**
         * Well known hardware addresses, big-endian byte order.
         */
        enum HardwareId {
            HWID_BROADCAST              = 0x0,      //!< Everybody should receive the packet
            HWID_SELF                   = 0xF10CC,  //!< Preprocessor HWID
        };

        /**
         * Commands used by different DAS modules.
         *
         * Add those gradualy as migrating from legacy software. Make sure
         * to document each one well when added.
         */
        enum CommandType {
            CMD_READ_VERSION            = 0x20, //!< Read module version
            CMD_READ_CONFIG             = 0x21, //!< Read module configuration
            CMD_READ_STATUS             = 0x22, //!< Read module status
            CMD_WRITE_CONFIG            = 0x30, //!< Write module configuration
            RSP_NACK                    = 0x40, //!< NACK to the command, the command that is being acknowledged is in payload[0] or payload[1]
            RSP_ACK                     = 0x41, //!< ACK to the command, the command that is being acknowledged is in payload[0] or payload[1]
            CMD_DISCOVER                = 0x80, //!< Discover modules
            CMD_START                   = 0x82, //!< Start acquisition
            CMD_STOP                    = 0x83, //!< Stop acquisition
            CMD_TSYNC                   = 0x84, //!< TSYNC packet
            CMD_RTDL                    = 0x85, //!< RTDL is a command packet, but can also be data packet if info == 0xFC
        };

        /**
         * Type of modules
         */
        enum ModuleType {
            MOD_TYPE_ROC                = 0x20,   //!< ROC (or LPSD) module
            MOD_TYPE_AROC               = 0x21,   //!< AROC
            MOD_TYPE_HROC               = 0x22,
            MOD_TYPE_BLNROC             = 0x25,
            MOD_TYPE_CROC               = 0x29,
            MOD_TYPE_IROC               = 0x2A,
            MOD_TYPE_BIDIMROC           = 0x2B,
            MOD_TYPE_DSP                = 0x30,
            MOD_TYPE_SANSROC            = 0x40,
            MOD_TYPE_ACPC               = 0xA0,
            MOD_TYPE_ACPCFEM            = 0xA1,
            MOD_TYPE_FFC                = 0xA2,
            MOD_TYPE_FEM                = 0xAA,
        };

        /**
         * Command packet info breakdown structure.
         */
        struct CommandInfo {
#ifdef BITFIELD_LSB_FIRST
            enum CommandType command:8;     //!< 8 bits describing DAS module commands
            enum ModuleType module_type:8;  //!< 15:8 bits describing module type
            unsigned lvds_parity:1;         //!< LVDS parity bit
            unsigned lvds_stop:1;           //!< Only last word in a LVDS packet should have this set to 1
            unsigned lvds_start:1;          //!< Only first word in a LVDS packet should have this set to 1
            unsigned lvds_cmd:1;            //!< Command(1)/Data(0) indicator
            unsigned unused:8;              //!< TODO: unknown
            unsigned is_chain:1;            //!< TODO: what is it?
            unsigned is_response:1;         //!< If 1 the packet is response to a command
            unsigned is_passthru:1;         //!< Not sure what this does, but it seems like it's getting set when DSP is forwarding the packet from some other module
            unsigned is_command:1;          //!< If 1, packet is command, data otherwise
#endif
        };

        /**
         * Data packet info breakdown structure.
         */
        struct DataInfo {
#ifdef BITFIELD_LSB_FIRST
            unsigned subpacket_start:1;     //!< The first packet in the train of subpackets
            unsigned subpacket_end:1;       //!< Last packet in the train
            unsigned only_neutron_data:1;   //!< Only neutron data, if 0 some metadata is included
            unsigned rtdl_present:1;        //!< Is RTDL 6-words data included right after the header? Should be always 1 for newer DSPs
            unsigned unused4:1;             //!< Always zero?
            unsigned format_code:3;         //!< Format code
            unsigned subpacket_count:16;    //!< Subpacket counter
            unsigned unused24_27:4;         //!< Not used, should be all 0
            unsigned last_subpacket:1;      //!< Is this the last subpacket?
            unsigned unused29:1;
            unsigned veto:1;                //!< Vetoed packet - XXX: Add more description
            unsigned is_command:1;          //!< If 1, packet is command, data otherwise
#endif
        };

        static const uint32_t MinLength = 6*4;  //!< Minumum total length of any DAS packet, at least the header must be present
        static const uint32_t MaxLength = 1800*8 + MinLength;   //!< Maximum total length of DAS packets

        uint32_t destination;                   //!< Destination id
        uint32_t source;                        //!< Sender id
        union {
            // Based on C99 standard 6.7.2.1 #10, structs are packed as tightly
            // as possible, so this union will always allocate the expected 4 bytes
            // The same standard chapter also allows compiler to choose bitfield
            // ordering. All it means that fields in a structure need to be reversed.
            // Not supporting it here right now since not able to test.
            uint32_t info;                      //!< Raw access to the info (dcomserver compatibility mode)
            struct CommandInfo cmdinfo;
            struct DataInfo datainfo;
        };                                      //!< 4-byte union of the info, it may include command specific values or data releated fields
        uint32_t payload_length;                //!< payload length, might include the RTDL at the start
        uint32_t reserved1;
        uint32_t reserved2;
        uint32_t payload[0];                    //!< 4-byte aligned payload data

        /**
         * Create DasPacket of variable size based on the payloadLength.
         *
         * @param[in] payloadLength Size of the packet payload in bytes.
         * @param[in] payload Payload to be copied into the DasPacket buffer, must match payloadLength. If 0, nothing will be copied.
         */
        static DasPacket *create(uint32_t payloadLength, const uint8_t *payload=0);

        /**
         * Check if packet is valid, like the alignment check, size check, etc.
         */
        bool valid() const;

        /**
         * Total length of the packet in bytes.
         */
        uint32_t length() const;

        /**
         * Is this a command packet?
         */
        bool isCommand() const;

        /**
         * Is this a response packet?
         */
        bool isResponse() const;

        /**
         * Is this a data packet?
         */
        bool isData() const;

        /**
         * Is this pure Neutron Event data packet?
         */
        bool isNeutronData() const;

        /**
         * Is this MetaData packet?
         */
        bool isMetaData() const;

        /**
         * Is this a RTDL packet?
         */
        bool isRtdl() const;

        /**
         * Return starting address of RtdlHeader in the packet or 0 if header not present.
         */
        const RtdlHeader *getRtdlHeader() const;

        /**
         * Return starting address of the Neutron Data regardless of RtdlHeader included or not.
         *
         * @param[out] count Number of NeutronEvents in the returned memory.
         */
        const NeutronEvent *getNeutronData(uint32_t *count) const;

        /**
         * Return the actual response type.
         *
         * Response packet command field does not always contain the response
         * to the command sent. In case where the packet passes through DSP,
         * some command responses get translated into RSP_ACK in the packet
         * header and the actual response type is 2 dword in the payload.
         * This function hides that complexity away and provides unified
         * way to get response type.
         *
         * @return Parsed command response type, or 0 if not response packet.
         */
        enum CommandType getResponseType() const;

        /**
         * Return the actual source hardware address.
         *
         * Response packet source field always contains hardware address
         * of the DSP. In case DSP is routing the packet for some other module,
         * the real source hardware address is in the payload.
         * This function returns the real source hardware address regardless
         * of where the packet is coming from.
         *
         * @see getPayload
         * @see getPayloadLength
         * @return Actual source hardware address.
         */
        uint32_t getSourceAddress() const;

        /**
         * Return address to the actual packet payload.
         *
         * The response packets behind the DSP contain their address in the
         * first 4-bytes of the payload. The real payload is thus shifted for
         * 4 bytes. This only applies to the command responses as the data
         * is being packed into bigger packets by the DSP.
         *
         * @see getSourceAddress
         * @see getPayloadLength
         * @return Returns address of the payload regardless where the packet
         *         is coming from.
         */
        const uint32_t *getPayload() const;

        /**
         * Length of the packet payload in bytes.
         *
         * Based on the origin of the packet, first 4 bytes of the payload
         * might contain the source hardware address of the module sending
         * the data.
         *
         * @see getPayload
         * @see getSourceAddress
         * @return Returns number of the actual payload.
         */
        uint32_t getPayloadLength() const;

#ifdef DWORD_PADDING_WORKAROUND
        uint32_t getAlignedLength() const {
            uint32_t len = length();
            if (((len + 7) & ~7) != len)
                len += 4;
            return len;
        }
#endif

    private:

        /**
         * Constructor.
         *
         * The constructor is kept private to prevent user make mistake allocating
         * this dynamically sizable object.
         *
         * @param[in] payloadLength Size of the packet payload in bytes.
         * @param[in] payload Payload to be copied into the DasPacket buffer, must match payloadLength. If 0, nothing will be copied.
         */
        DasPacket(uint32_t payloadLength, const uint8_t *payload=0);
};

#endif // DASPACKET_HPP
