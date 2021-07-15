/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef RTDL_HEADER_HPP
#define RTDL_HEADER_HPP

#include <stdint.h>

/**
 * Structure representing RTDL header.
 *
 * RTDL header is present in all data packets originating from DSP-T, but not
 * from legacy DSP. There's a flag in data DasPacket that indicates
 * whether RTDL header is present or not.
 *
 * RTDL header is also present in RTDL packet coming from DSP-T. Header is
 * followed by 26 frames of RTDL data from accelerator.
 */
struct RtdlHeader {

    /**
     * Pulse flavor as described in Chapter 1.3.4 of
     * SNS Timing Master Functional System description
     * document.
     */
    enum PulseFlavor {
        RTDL_FLAVOR_NO_BEAM         = 0,    //!< No Beam
        RTDL_FLAVOR_TARGET_1        = 1,    //!< Normal Beam (Target 1)
        RTDL_FLAVOR_TARGET_2        = 2,    //!< Normal Beam (Target 2)
        RTDL_FLAVOR_DIAG_10US       = 3,    //!< 10 uSecond Diagnostic Pulse (not used)
        RTDL_FLAVOR_DIAG_50US       = 4,    //!< 50 uSecond Diagnostic Pulse
        RTDL_FLAVOR_DIAG_100US      = 5,    //!< 100 uSecond Diagnostic Pulse
        RTDL_FLAVOR_PHYSICS_1       = 6,    //!< Special Physics Pulse 1
        RTDL_FLAVOR_PHYSICS_2       = 7,    //!< Special Physics Pulse 2
    };

    /**
     * Previous cycle veto status as described in Chapter 5.1.8 of
     * SNS Timing Master Functional System description
     * document.
     */
    enum CycleVeto {
        RTDL_VETO_NO_BEAM           = (1 << 0), //!< No beam was delivered on the previous pulse.
        RTDL_VETO_NOT_TARGET_1      = (1 << 1), //!< Beam was delivered to target 2 (not to target 1)
        RTDL_VETO_NOT_TARGET_2      = (1 << 2), //!< Beam was delivered to target 1 (not to target 2)
        RTDL_VETO_DIAGNOSTIC_PULSE  = (1 << 3), //!< Beam was a “reduced intensity” diagnostic pulse
        RTDL_VETO_PHYSICS_PULSE_1   = (1 << 4), //!< Beam was one of the special physics study pulses
        RTDL_VETO_PHYSICS_PULSE_2   = (1 << 5), //!< Beam was one of the special physics study pulses (the other type)
        RTDL_VETO_MPS_AUTO_RESET    = (1 << 6), //!< Beam was interrupted by an “Auto Reset” MPS trip (fast protect)
        RTDL_VETO_MPS_FAULT         = (1 << 7), //!< Beam was interrupted or not delivered because of a “Latched” MPS trip
        RTDL_VETO_EVENT_LINK_ERROR  = (1 << 8), //!< Timing system detected corruption on the event link
        RTDL_VETO_RING_RF_SYNCH     = (1 << 9), //!< Timing system has lost synch with the Ring RF signal
        RTDL_VETO_RING_RF_FREQ      = (1 << 10), //!< Measured ring RF frequency is outside acceptable range
        RTDL_VETO_60_HZ_ERROR       = (1 << 11), //!< 60 Hz line phase error is out of tolerance
    };

    uint32_t timestamp_sec;
    uint32_t timestamp_nsec;
    union {
        uint32_t charge;
#ifdef BITFIELD_LSB_FIRST
        struct {
            unsigned charge:24;         //!< Pulse charge in 10 pC unit
            enum PulseFlavor flavor:6;  //!< Pulse flavor of the next cycle
            unsigned bad:1;             //!< Bad pulse flavor frame
            unsigned unused31:1;        //!< not used
        } pulse;
#endif
    };
    union {
        uint32_t general_info;
#ifdef BITFIELD_LSB_FIRST
        struct {
            unsigned cycle:10;          //!< Cycle number
            unsigned last_cycle_veto:12;//!< Last cycle veto
            unsigned tstat:8;           //!< TSTAT
            unsigned bad_cycle_frame:1; //!< Bad cycle frame
            unsigned bad_veto_frame:1;  //!< Bad last cycle veto frame
        };
#endif
    };
    uint32_t tsync_period;
    union {
        uint32_t tsync_delay;
#ifdef BITFIELD_LSB_FIRST
        struct {
            unsigned tof_fixed_offset:24; //!< TOF fixed offset
            unsigned frame_offset:4;    //!< RTDL frame offset
            unsigned unused28:3;        //!< "000"
            unsigned tof_full_offset:1; //!< TOF full offset enabled
        };
#endif
    };
};

#endif // RTDL_HEADER_HPP
