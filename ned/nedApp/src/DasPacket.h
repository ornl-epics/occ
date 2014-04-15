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
         * Well known hardware addresses.
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
            CMD_WRITE_CONFIG            = 0x30, //!< Write module configuration
            CMD_DISCOVER                = 0x80, //!< Discover modules
            CMD_START                   = 0x82, //!< Start acquisition
            CMD_STOP                    = 0x83, //!< Stop acquisition
            CMD_RTDL                    = 0x85, //!< RTDL is a command packet, but can also be data packet if info == 0xFC
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
            struct {
#ifdef BITFIELD_LSB_FIRST
                enum CommandType command:8;    //!< 8 bits describing DAS module commands
                unsigned unused:20;             //!< LVDS parity bits used to be here, may still be present in responses
                unsigned is_chain:1;            //!< TODO: what is it?
                unsigned is_response:1;         //!< If 1 the packet is response to a command
                unsigned is_passthru:1;         //!< Not sure what this does, but it seems like it's getting set when DSP is forwarding the packet from some other module
                unsigned is_command:1;          //!< If 1, packet is command, data otherwise
#endif
            } cmdinfo;
            struct {
#ifdef BITFIELD_LSB_FIRST
                unsigned subpacket_start:1;     //!< I guess the first packet in the train of subpackets
                unsigned subpacket_end:1;       //!< End of subpacket, only the last packet of all subpackets has this one set
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
            } datainfo;
        };                                      //!< 4-byte union of the info, it may include command specific values or data releated fields
        uint32_t payload_length;                //!< payload length, might include the RTDL at the start
        uint32_t reserved1;
        uint32_t reserved2;
        uint32_t data[0];

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
         * Length of the packet payload in bytes.
         */
        uint32_t payloadLength() const;

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
