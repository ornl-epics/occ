/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 */

#ifndef I2C_H
#define I2C_H

#define I2CR                                    0x320

#define I2CR_SDA                                0x00000004L
#define I2CR_SDA_HIGH                           0x00000004L
#define I2CR_SDA_LOW                            0x00000000L

#define I2CR_SCL                                0x00000002L
#define I2CR_SCL_HIGH                           0x00000002L
#define I2CR_SCL_LOW                            0x00000000L

#define I2CR_OE                                 0x00000008L
#define I2CR_OE_HIGH                            0x00000008L
#define I2CR_OE_LOW                             0x00000000L

#define I2CR_SDA_PIN                            0x00000001L
#define I2CR_DEFAULT            (I2CR_OE_HIGH | I2CR_SCL_HIGH | I2CR_SDA_HIGH)

#define SUCCESS                                 1
#define ERROR                                   0

//#define delay(i1)     Sleep(i1)

typedef unsigned char byte;
typedef unsigned int  word;

#include "occlib.h"

/*********************************************************************************/
/*                           Global Routines                                     */
/*********************************************************************************/
unsigned int Read_I2C_Bus(struct occ_handle *occ, byte address, byte offset, word *data);

#endif

