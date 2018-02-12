/* PacketList.h
 *
 * Copyright (c) 2017 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

#ifndef PACKET_H
#define PACKET_H

#include "RtdlHeader.h"

#include <cassert>
#include <stdint.h>
#include <cstddef>
#include <vector>
#include <stdexcept>

/**
 * The packet structure describes any given packet exchanged between plugins.
 */
class Packet {
    public:
        typedef enum {
            TYPE_LEGACY     = 0x0,
            TYPE_ERROR      = 0x1,
            TYPE_RTDL       = 0x6,
            TYPE_DAS_DATA   = 0x7,
            TYPE_DAS_CMD    = 0x8,
            TYPE_ACC_TIME   = 0x10,
            TYPE_TEST       = 0xFE,
            TYPE_OLD_RTDL   = 0xFF, // Software only, hopefully such packet doesn't get defined
        } Type;

        struct __attribute__ ((__packed__)) {
            unsigned sequence:8;    //!< Packet sequence number, incremented by sender for each sent packet
            bool priority:1;        //!< Flag to denote high priority handling, used by hardware to optimize interrupt handling
            unsigned __reserved1:11;
            Type type:8;            //!< Packet type
            unsigned version:4;     //<! Packet version
        };

        uint32_t length;            //!< Total number of bytes for this packet

    public: /* Functions */

        /**
         * Cast raw pointer to Packet pointer.
         * 
         * The function tries to interpret raw data as a valid Packet,
         * performing several checks including but not limited to:
         * - checking minimum/maximum packet size requirements
         * - checking enough memory is allocated for a packet
         *
         * @return Casted valid packet, throws otherwise
         */
        static const Packet *cast(const uint8_t *data, size_t size) throw(std::runtime_error);
        
        /**
         * Performs integrity check on packet, invokes derived functions if necessary.
         */
        bool verify(uint32_t &errorOffset) const;
};

/**
 * Error packet is produced by the receiver hardware when incoming packet is not valid.
 */
class ErrorPacket : public Packet {
    public:
        typedef enum {
            TYPE_NO_ERROR   = 0x0,
            TYPE_ERR_FRAME  = 0x1,
            TYPE_ERR_LENGTH = 0x2,
            TYPE_ERR_CRC    = 0x3,
        } ErrorCode;

        struct __attribute__ ((__packed__)) {
            unsigned __err_rsv1:8;
            ErrorCode code:4;       //!< Type of error detected
            unsigned __err_rsv2:20;
        };
        uint32_t frame_count;       //!< Number of frame errors
        uint32_t length_count;      //!< Number of length errors
        uint32_t crc_count;         //!< Number of CRC errors
        uint32_t orig[0];           //!< Recovered data, dynamic length defined by packet length field
};

class DasDataPacket : public Packet {
    public: /* Variables */
        typedef enum {
            EVENT_FMT_RESERVED       = 0,
            EVENT_FMT_META           = 1,    //!< meta data (for choppers, beam monitors, ADC sampling etc.) in tof,pixel format
            EVENT_FMT_PIXEL          = 2,    //!< neutron data in tof,pixel format
            EVENT_FMT_LPSD_RAW       = 16,   //!< LPSD raw format
            EVENT_FMT_LPSD_VERBOSE   = 17,   //!< LPSD verbose format
            EVENT_FMT_ACPC_XY_PS     = 18,   //!< X,Y,Photo sum format
            EVENT_FMT_ACPC_RAW       = 19,   //!< ACPC raw format
            EVENT_FMT_ACPC_VERBOSE   = 20,   //!< ACPC verbose format
            EVENT_FMT_AROC_RAW       = 21,   //!< AROC raw format
            EVENT_FMT_BNL_XY         = 22,   //!< X,Y format
            EVENT_FMT_BNL_RAW        = 23,   //!< BNL raw format
            EVENT_FMT_BNL_VERBOSE    = 24,   //!< BNL verbose format
            EVENT_FMT_CROC_RAW       = 25,   //!< CROC raw format
            EVENT_FMT_CROC_VERBOSE   = 26,   //!< CROC verbose format
        } EventFormat;

        struct __attribute__ ((__packed__)) {
            uint16_t num_events;            //!< Number of events
            EventFormat event_format:8;     //!< Data format
            bool mapped:1;                  //!< Flag whether events are mapped to logical ids
            bool corrected:1;               //!< Flag whether geometrical correction has been applied
            unsigned __data_rsv2:6;
        };

        uint32_t timestamp_sec;             //!< Accelerator time (seconds) of event 39
        uint32_t timestamp_nsec;            //!< Accelerator time (nano-seconds) of event 39

        uint32_t events[0];                 //!< Placeholder for dynamic buffer of events

    public: /* Functions */
        /**
         * Templated function to cast generic packet events to the format of callers' preference.
         *
         * Function does a simple cast and does not check for format integrity.
         * As a convenience it also returns number of events based on the
         * requested event format.
         */
        template<typename T>
        T *getEvents(uint32_t &count)
        {
            assert(sizeof(T) % 4 == 0);
            count = (this->length - sizeof(DasDataPacket)) / sizeof(T);
            return reinterpret_cast<T *>(this->events);
        }
        
        bool verify(uint32_t &errorOffset) const;
};

class RtdlPacket : public Packet {
    public:
        struct __attribute__ ((__packed__)) RtdlFrame {
            union __attribute__ ((__packed__)) {
                struct __attribute__ ((__packed__)) {
                    unsigned data:24;               //!< RTDL frame data
                    uint8_t id;                     //!< RTDL frame identifier
                };
                uint32_t raw;                       //!< Non decoded RTDL frame
            };
            RtdlFrame(uint32_t raw_)
            : raw(raw_) {}
            RtdlFrame(uint8_t id_, uint32_t data_)
            : data(data_ & 0xFFFFFF)
            , id(id_) {}
        };

        struct __attribute__ ((__packed__)) {
            uint8_t num_frames;             //!< Number of 4 byte RTDL frames in this packet
            unsigned __reserved2:24;
        };

        RtdlFrame frames[0];                //!< Placeholder for dynamic buffer of RTDL frame data

    public: /* Functions */
        /**
         * Return number of RTDL frames included in this packet.
         */
        uint8_t getNumRtdlFrames() const {
            return num_frames;
        }

        /**
         * Return RTDL frames from the packet in no particular order.
         */
        std::vector<RtdlFrame> getRtdlFrames() const {
            return std::vector<RtdlFrame>(frames, frames + num_frames);
        }
        
        bool verify(uint32_t &errorOffset) const;
};

class DasCmdPacket : public Packet {
    public: /* Variables */

        struct {
            unsigned cmd_length:12;     //!< Command payload length in bytes, must be multiple of 2
            unsigned __reserved2:4;
            unsigned command:8;         //!< Type of command
            unsigned cmd_id:5;          //!< Command/response verification id
            bool acknowledge:1;         //!< Flag whether command was succesful, only valid in response
            bool response:1;            //!< Flags this command packet as response
            unsigned cmd_version:1;     //!< LVDS protocol version,
                                        //!< hardware uses this flag to distinguish protocol in responses
                                        //!< but doesn't use it from optical side.
        };
        uint32_t module_id;             //!< Destination address
        uint32_t payload[0];            //!< Dynamic sized command payload, storage must be multiple of 4 bytes but actual payload can be multiple of 2

    public: /* Functions */
};

class TestPacket : public Packet {
    public: /* Variables */

        uint32_t hdr1;
        union {
            uint32_t hdr2;
            struct {
                unsigned data_len:24;
                uint8_t data_type;
            };
        };
        uint32_t hdr3;
        uint32_t hdr4;
        uint32_t hdr5;
        uint32_t hdr6;
        uint32_t hdr7;
        uint32_t hdr8;
        uint32_t payload[0];            //!< Dynamic sized data container

    public: /* Functions */
        bool verify(uint32_t &errorOffset) const;
};

#endif // PACKET_H
