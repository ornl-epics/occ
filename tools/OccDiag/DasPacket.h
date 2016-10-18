/* DasPacket.h
 *
 * Copyright (c) 2014 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

#ifndef DASPACKET_HPP
#define DASPACKET_HPP

#include "RtdlHeader.h"

#include <stdint.h>

#define ALIGN_UP(number, boundary)      (((number) + (boundary) - 1) & ~((boundary) - 1))

/**
 * Class representing a DAS packet.
 *
 * DasPacket is a container for data transfered over OCC link. It's a structure
 * representing one DAS packet. DAS packet is dynamic in size and the actual
 * length is defined by the header. Header is the first 6 4-byte entities and
 * defines the rest of the data. Packet size is limited by DSP to around
 * 3624 bytes and is always 4-byte aligned. When actual payload does not align
 * on 4-byte boundary, data is padded. The length header parameter always
 * defines the actual size of the packet.
 *
 * Packets are received from DSP through OCC board. They can be distinguished
 * into two groups, data and commands packets. When DSP receives data from
 * submodules, it merges data from many submodules into data packets of payload
 * size max 3600 bytes. Commands and responses are treated as command packets
 * and their flow is described with next picture.
 *
 * @image html OCC_protocol_packing.png
 * @image latex OCC_protocol_packing.png width=6in
 *
 * Don't introduce any virtual functions here as this will break allocating
 * objects from OCC buffer.
 */
struct DasPacket
{
    public: // Supporting structures and enums
        /**
         * Structure representing single Event
         */
        struct Event {
            uint32_t tof;
            uint32_t pixelid;
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
         * Add those gradually as migrating from legacy software. Make sure
         * to document each one well when added.
         */
        enum CommandType {
            CMD_READ_VERSION            = 0x20, //!< Read module version
            CMD_READ_CONFIG             = 0x21, //!< Read module configuration
            CMD_READ_STATUS             = 0x22, //!< Read module status
            CMD_READ_TEMPERATURE        = 0x23, //!< Read module temperature(s)
            CMD_READ_STATUS_COUNTERS    = 0x24, //!< Read module status counters
            CMD_RESET_STATUS_COUNTERS   = 0x25, //!< Reset module status counters
            CMD_RESET_LVDS              = 0x27, //!< Reset LVDS chips
            CMD_TC_RESET_LVDS           = 0x28, //!< Send a short T&C SysReset signal
            CMD_TC_RESET                = 0x29, //!< Send a long T&C SysReset signal
            CMD_WRITE_CONFIG            = 0x30, //!< Write module configuration
            CMD_WRITE_CONFIG_1          = 0x31, //!< Write module configuration section 1
            CMD_WRITE_CONFIG_2          = 0x32, //!< Write module configuration section 2
            CMD_WRITE_CONFIG_3          = 0x33, //!< Write module configuration section 3
            CMD_WRITE_CONFIG_4          = 0x34, //!< Write module configuration section 4
            CMD_WRITE_CONFIG_5          = 0x35, //!< Write module configuration section 5
            CMD_WRITE_CONFIG_6          = 0x36, //!< Write module configuration section 6
            CMD_WRITE_CONFIG_7          = 0x37, //!< Write module configuration section 7
            CMD_WRITE_CONFIG_8          = 0x38, //!< Write module configuration section 8
            CMD_WRITE_CONFIG_9          = 0x39, //!< Write module configuration section 9
            CMD_WRITE_CONFIG_A          = 0x3A, //!< Write module configuration section A
            CMD_WRITE_CONFIG_B          = 0x3B, //!< Write module configuration section B
            CMD_WRITE_CONFIG_C          = 0x3C, //!< Write module configuration section C
            CMD_WRITE_CONFIG_D          = 0x3D, //!< Write module configuration section D
            CMD_WRITE_CONFIG_E          = 0x3E, //!< Write module configuration section E
            CMD_WRITE_CONFIG_F          = 0x3F, //!< Write module configuration section F
            RSP_NACK                    = 0x40, //!< NACK to the command, the command that is being acknowledged is in payload[0] or payload[1]
            RSP_ACK                     = 0x41, //!< ACK to the command, the command that is being acknowledged is in payload[0] or payload[1]
            BAD_PACKET                  = 0x42, //!< Bad packet
            CMD_HV_SEND                 = 0x50, //!< Send data through RS232 port, HV connected to ROC
            CMD_HV_RECV                 = 0x51, //!< Receive data from RS232 port, HV connected to ROC
            CMD_EEPROM_ERASE            = 0x60, //!< Erase device EEPROM
            CMD_EEPROM_LOAD             = 0x61, //!< Load data from EEPROM (?)
            CMD_EEPROM_READ             = 0x62, //!< Read contents of EEPROM and return (?)
            CMD_EEPROM_WRITE            = 0x63, //!< Write contents of EEPROM (?)
            CMD_EEPROM_READ_WORD        = 0x64, //!< Read single word from EEPROM
            CMD_EEPROM_WRITE_WORD       = 0x65, //!< Write single word to EEPROM
            CMD_UPGRADE                 = 0x6F, //!< Send chunk of new firmware data
            CMD_DISCOVER                = 0x80, //!< Discover modules
            CMD_RESET                   = 0x81, //!< Reset of all components
            CMD_START                   = 0x82, //!< Start acquisition
            CMD_STOP                    = 0x83, //!< Stop acquisition
            CMD_TSYNC                   = 0x84, //!< TSYNC packet
            CMD_RTDL                    = 0x85, //!< RTDL is a command packet, but can also be data packet if info == 0xFC
            CMD_PM_PULSE_RQST_ON        = 0x90, //!< Request one pulse for Pulsed Magnet
            CMD_PM_PULSE_RQST_OFF       = 0x91, //!< Clears one pulse request for Pulsed Magnet
            CMD_PREAMP_TEST_CONFIG      = 0x92, //!< Send pulse settings
            CMD_PREAMP_TEST_TRIGGER     = 0x93, //!< Send a single pre-amp pulse request
        };

        /**
         * Type of modules
         */
        enum ModuleType {
            MOD_TYPE_ROC                = 0x20,   //!< ROC (or LPSD) module
            MOD_TYPE_AROC               = 0x21,   //!< AROC
            MOD_TYPE_HROC               = 0x22,
            MOD_TYPE_BNLROC             = 0x25,
            MOD_TYPE_CROC               = 0x29,
            MOD_TYPE_IROC               = 0x2A,
            MOD_TYPE_BIDIMROC           = 0x2B,
            MOD_TYPE_ADCROC             = 0x2D,
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
            union __attribute__ ((__packed__)) {
                enum ModuleType module_type:8;  //!< Module type valid in CMD_DISCOVER responses
                struct __attribute__ ((__packed__)) {
                    unsigned channel:4;     //!< Channel number, starting from 0
                    unsigned is_channel:1;  //!< Is this command for channel?
                    unsigned chan_fill:3;   //!< Not used, always 0
                };
            };
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
            unsigned format_code:3;         //!< Format code, 000 for neutron data, 111 for RTDL data packet
            unsigned subpacket_count:16;    //!< Subpacket counter
            unsigned unused24_27:4;         //!< Not used, should be all 0
            unsigned last_subpacket:1;      //!< Is this the last subpacket?
            unsigned unused29:1;
            unsigned veto:1;                //!< Vetoed packet - XXX: Add more description
            unsigned is_command:1;          //!< If 1, packet is command, data otherwise
#endif
        };

        static const uint32_t MinLength;    //!< Minumum total length of any DAS packet, at least the header must be present
        static const uint32_t MaxLength;    //!< Maximum total length of DAS packets

    public: // Structure definition - represents OCC header
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

        uint32_t payload[0];                    //!< 4-byte aligned payload data, support empty packets

        static DasPacket *create(uint32_t payloadLength, const uint8_t *payload=0);

    public: // functions
        /**
         * Create DasPacket OCC command (for DSPs)
         *
         * When sending commands to DSP, the packet format is very simple.
         * The OCC header defines common fields and the payload is the actual
         * little-endian data in units of 4 bytes.
         *
         * @param[in] source address of the sender
         * @param[in] destination address
         * @param[in] command type for new packet
         * @param[in] channel target channel, 0 for no specific channel.
         * @param[in] payload_length Size of the packet payload in bytes.
         * @param[in] payload Payload to be copied into the DasPacket buffer, must match payloadLength. If 0, nothing will be copied.
         */
        static DasPacket *createOcc(uint32_t source, uint32_t destination, CommandType command, uint8_t channel, uint32_t payload_length, uint32_t *payload = 0);

        /**
         * Create DasPacket LVDS command (non DSPs)
         *
         * Other modules are connected to DSP thru LVDS link. The DSP simply
         * passes thru the data it receives, so the software must format
         * the package as expected by modules. LVDS is 21-bit
         * bus and the protocol itself uses 5 control bits. There's 16 bits for data.
         *
         * The payload must be 4-byte aligned memory block, but the payload_length
         * can be 2 bytes less than the memory length. The payload_length
         * is put in the packet header, the transmit unit is 4 bytes. Code that
         * transmits data to OCC takes length from the packet header and aligns
         * it up to 4-byte boundary. Received packets are always 4-byte aligned,
         * courtesy of DSP.
         *
         * This functions re-formats payload data into LVDS data, taking care
         * of protocol flags and packing it into OCC packet. The result
         * is the packet about twice the size of the original payload.
         *
         * @param[in] source address of the sender
         * @param[in] destination address
         * @param[in] command type for new packet
         * @param[in] channel target channel, 0 for no specific channel.
         * @param[in] payload_length Size of the packet payload in bytes.
         * @param[in] payload Payload to be copied into the DasPacket buffer, must match payloadLength. If 0, nothing will be copied.
         */
        static DasPacket *createLvds(uint32_t source, uint32_t destination, CommandType command, uint8_t channel, uint32_t payload_length, uint32_t *payload = 0);

        /**
         * Check if packet is valid, like the alignment check, size check, etc.
         */
        bool isValid() const;

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
         * Is this a bad packet as identified by OCC board?
         */
        bool isBad() const;

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
         * Return router hardware address or 0 if response is coming from router.
         */
        uint32_t getRouterAddress() const;

        /**
         * Return address to the actual packet payload.
         *
         * The packet payload is all data after the protocol header.
         * Protocol header is 24 bytes long. For modules behind DSP there's
         * optional 4 or 8 bytes of data describing the sub-module address
         * and response type.
         * Everything else is considered payload, including the RTDL information
         * provided by DSP-T in data packets.
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
         * @return Returns length of the packet payload in bytes.
         */
        uint32_t getPayloadLength() const;


        /**
         * Return pointer to packet payload data regardless of RtdlHeader included or not.
         *
         * Data packets always originate from DSP. DSP aggregates data from all
         * sub-modules into data packets. There is never LVDS header information
         * in data packets. But there is always RTDL header when data packets
         * are created by DSP-T.
         *
         * Function checks packet integrity and returns invalid address in case
         * of any error. Caller should always check the return address or count
         * parameter.
         *
         * @param[out] count Number of 4-byte blocks in the returned address or 0 on error.
         * @return Starting address of the payload data or 0 on error.
         */
        const uint32_t *getData(uint32_t *count) const;

        /**
         * Return pointer to packet payload data regardless of RtdlHeader included or not.
         *
         * Function checks packet integrity and returns invalid address in case
         * of any error. Caller should always check the return address or count
         * parameter.
         *
         * @param[out] count Number of 4-byte dwords in returned address or 0 on error.
         * @return Starting address of the payload data or 0 on error.
         */
        uint32_t *getData(uint32_t *count) {
            return const_cast<uint32_t *>(const_cast<const DasPacket *>(this)->getData(count));
        }

        /**
         * Return the length of real packet data, excluding RTDL information.
         *
         * @see getData
         * @return Returns length of the packet data in bytes.
         */
        uint32_t getDataLength() const;

        /**
         * Copy header and RTDL header of this container to another one.
         * @param[out] dest Previously allocated packet where to copy header
         * @param[in] destSize Size of allocated destination packet - must be
         *                     greater than this packet size or function fails.
         * @return true on success, fail on failure
         */
        bool copyHeader(DasPacket *dest, uint32_t destSize) const;

    private:

        /**
         * Constructor for creating command packets
         *
         * The constructor is kept private to prevent user make mistake allocating
         * this dynamically sizable object.
         *
         * @param[in] source address of the sender
         * @param[in] destination address
         * @param[in] cmdinfo describing newly created command packet
         * @param[in] payload_length Size of the packet payload in bytes.
         * @param[in] payload Payload to be copied into the DasPacket buffer, must match payloadLength. If 0, nothing will be copied.
         */
        DasPacket(uint32_t source, uint32_t destination, CommandInfo cmdinfo, uint32_t payload_length, uint32_t *payload = 0);

        /**
         * Calculate and return even parity bit of the number given.
         */
        static bool lvdsParity(int number);
};

#endif // DASPACKET_HPP
