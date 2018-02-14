/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef DASPACKET_HPP
#define DASPACKET_HPP

#include <stdint.h>
#include <stdexcept>

#define ALIGN_UP(number, boundary)      (((number) + (boundary) - 1) & ~((boundary) - 1))

/**
 * Class representing a DAS packet.
 */
struct DasPacket
{
        uint32_t destination;                   //!< Destination id
        uint32_t source;                        //!< Sender id
        uint32_t cmdinfo;                       //!< Bitfield describing the packet
        uint32_t payload_length;                //!< payload length, might include the RTDL at the start
        uint32_t reserved1;
        uint32_t reserved2;

        uint32_t payload[0];                    //!< 4-byte aligned payload data, support empty packets

        /**
         * Cast raw pointer to DasPacket pointer.
         * 
         * @return Casted valid packet, throws otherwise
         */
        static const DasPacket *cast(const uint8_t *data, size_t size) throw(std::runtime_error)
        {
            if (size < sizeof(DasPacket)) {
                throw std::runtime_error("Not enough data to describe packet header");
            }

            const DasPacket *packet = reinterpret_cast<const DasPacket *>(data);

            if (packet->payload_length != ALIGN_UP(packet->payload_length, 4)) {
                throw std::runtime_error("Invalid packet length");
            }

            if (packet->payload_length > 32768) {
                throw std::runtime_error("Packet length out of range");
            }

            return packet;
        }
};

#endif // DASPACKET_HPP
