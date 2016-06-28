#include "i2c.h"
#include "occlib_drv.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

///Local Routines
static void start_signal(struct occ_handle *occ, byte device);
static void stop_signal(struct occ_handle *occ);
static void read_signal(struct occ_handle *occ);
static void write_signal(struct occ_handle *occ);
static int read_acknowledge(struct occ_handle *occ);
static void write_byte_bus(struct occ_handle *occ, byte write_byte_data);
static int  read_byte_bus(struct occ_handle *occ);
static void Outport32(struct occ_handle *occ, uint32_t offset, uint32_t data);
static void Bit_Outport32(struct occ_handle *occ, uint32_t offset, uint32_t bit_pattern, uint32_t value);

static uint8_t pcie_bar = 0; 

#define SLEEP_VALUE 250

/*******************************************************************************/
// void Outport32(uint32_t offset, uint32_t data);
//
//		handle - handle to device driver
// 		bar - PCIe Base Address
//      offset - byte offset within BAR
//      data - data to be written      
//
//    Output data to OCC register
//
/*******************************************************************************/
static void Outport32(struct occ_handle *occ, uint32_t offset, uint32_t data)
{
        uint32_t occ_data[1];
        occ_data[0] = data;
        //printf("occ_data = %x \n", occ_data[0]);
	int ret = occdrv_io_write(occ, pcie_bar, offset, occ_data, 1);
    if (ret < 0) {
        fprintf(stderr, "ERROR: cannot read BAR%d at offset 0x%08X - %s\n", pcie_bar, offset, strerror(-ret));
    } else {
        //printf("Written %d dwords to BAR%d at offset 0x%08X: data = %x \n", ret, pcie_bar, offset,occ_data[0]);
    }
}

/*******************************************************************************/
// unsigned long Inport32(uint32_t offset);
//
//      offset - byte offset within BAR
//
//    Input data from OCC register
//
/*******************************************************************************/
unsigned long Inport32(struct occ_handle *occ, uint32_t offset)
{
  uint32_t data[4];

  int ret = occdrv_io_read(occ, pcie_bar, offset, data, 1);
  if (ret < 0) {
      fprintf(stderr, "ERROR: cannot read BAR%d at offset 0x%08X - %s\n", pcie_bar, offset, strerror(-ret));
	  return(0xFFFFFFFF);
  } else {
      return(data[0]);
  }
}

/*******************************************************************************/
// void Bit_Outport32(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t bit_mask, uint32_t value)
//
//		handle - handle to device driver
// 		bar - PCIe Base Address
//      offset - byte offset within BAR
//      bit_mask - select bit to modify
//      value - set the bit to this
//
//    This function writes selected bits in the specified register
//
/*******************************************************************************/
void Bit_Outport32(struct occ_handle *occ, uint32_t offset, uint32_t bit_mask, uint32_t value)
{
    // do a read modify write to the selected port, only modifing the requested bits
    Outport32(occ, offset, ((Inport32(occ, offset) & ~bit_mask) | (value & bit_mask)));
}

/*******************************************************************************/
// static void start_signal(byte device);
//
//       device - Address of the device.
//
//    Routine to generate a start signal plus the identifier and the serial bus  
//    address. A start signal is defined as:  
//            "SDA goes low when SCL is high".
/*******************************************************************************/
static void start_signal(struct occ_handle *occ, byte device)
{
   byte bit_mask;

   // enable data output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_HIGH);     // OE high
   usleep(SLEEP_VALUE);

   // Set the clock pin high
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL);

   // Set the data line low
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);   // SDA low
   usleep(SLEEP_VALUE);

   // Set the clock line low
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
   usleep(SLEEP_VALUE);

   // set the mask to one
   bit_mask = 0x40;

   // write the device identifier and the address
   while (bit_mask >= 1)
      {
      if (device & bit_mask)
         {
         // Set the data line high
         Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
         usleep(SLEEP_VALUE);
         }

      else
         {
         // set the data line low
         Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);   // SDA low
         usleep(SLEEP_VALUE);
         }

      // clock the data
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
      usleep(SLEEP_VALUE);

      // Set the clock line low
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
      usleep(SLEEP_VALUE);

      // shift the bit mask
      bit_mask >>= 1;
      }
}

/*********************************************************************************/
// static void stop_signal(void);
//
//    Routine to generate a stop signal.  A stop signal is defined as:
//                 "SDA goes high when SCL is high".
//
/*********************************************************************************/
static void stop_signal(struct occ_handle *occ)
{

   // enable data output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_HIGH);     // OE high
   usleep(SLEEP_VALUE);

   // take the data line low
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);
   usleep(SLEEP_VALUE);

   // clock the data
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
   usleep(SLEEP_VALUE);

   // take the data line high
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
   usleep(SLEEP_VALUE);
}

/*********************************************************************************/
// static void read_signal(void);
//
//    Routine to generate the read signal.
//
//       R/W* bit = 1 in the data stream = read operation
//
/*********************************************************************************/
static void read_signal(struct occ_handle *occ)
{
   // take the data line high
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
   usleep(SLEEP_VALUE);

   // clock the data
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
   usleep(SLEEP_VALUE);

   // disable the SDA output enable ---JEB
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_LOW);     // disable Output enable
   usleep(SLEEP_VALUE);

   // Set the clock line low
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
   usleep(SLEEP_VALUE);
}

/*********************************************************************************/
// static void write_signal(void);
//
//    Routine to generate the write signal.
//
//       R/W* bit = 0 in the data stream = write operation
//
/*********************************************************************************/
static void write_signal(struct occ_handle *occ)
{
   // set the data line low
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);   // SDA low
   usleep(SLEEP_VALUE);

   // clock the data
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
   usleep(SLEEP_VALUE);

   // Set the clock line low
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
   usleep(SLEEP_VALUE);
}

/*********************************************************************************/
//    static void write_acknowlegde(void);
//
// Description:
//
//    Routine to write the acknowledge when a data byte is read from a slave
//
/*********************************************************************************/
static void write_acknowledge(struct occ_handle *occ)
{
   // enable data output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_HIGH);    // OE high
   usleep(SLEEP_VALUE);

   // set the data line low
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);   // SDA low
   usleep(SLEEP_VALUE);

   // clock the data
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
   usleep(SLEEP_VALUE);

   // Set the clock line low
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
   usleep(SLEEP_VALUE);

   // Set the data line high
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
   usleep(SLEEP_VALUE);
}

/*********************************************************************************/
// static int read_acknowledge(void);
//
//    Routine to read the acknowledge bit following a transfer to a slave
//
/*********************************************************************************/
static int read_acknowledge(struct occ_handle *occ)
{
   byte I2C_input;

   //printf("In read_acknowledge \n");


   // Set the data line high
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
   usleep(SLEEP_VALUE);

   // disable data output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_LOW);     // OE low
   usleep(SLEEP_VALUE);

   I2C_input = (byte) (Inport32(occ, I2CR) & I2CR_SDA_PIN);

   // clock the data
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
   usleep(SLEEP_VALUE);

   // Set the clock line low
   Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
   usleep(SLEEP_VALUE);

   // if acknowldege is not zero then error
   if (I2C_input != 0)
      {
#ifdef STATUS
      printf("I2C acknowldege did not go low\n");
#endif
      return(ERROR);
      }

   // return success
   else
      return(SUCCESS);
}

/*********************************************************************************/
// static void write_byte_bus(byte write_byte_data);
//
//       write_byte_data - Data to write on the bus.
//
//    Routine to write a byte of data to the serial EEPROM.
//
/*********************************************************************************/
static void write_byte_bus(struct occ_handle *occ, byte write_byte_data)
{
   byte bit_mask;

   // enable data output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_HIGH);     // OE high
   usleep(SLEEP_VALUE);


   // set the mask to one
   bit_mask = 0x80;

   // for each bit
   while (bit_mask >= 1)
      {
     // set the data line high
      if (bit_mask & write_byte_data)
         {
	 //printf("writing '1': bitmask = %x write_byte_data = %x \n", bit_mask, write_byte_data); 

         // Set the data line high
         Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
         usleep(SLEEP_VALUE);
         }

      // set the data line low
      else
         {
	 //printf("writing '0': bitmask = %x write_byte_data = %x \n", bit_mask, write_byte_data); 
         // set the data line low
         Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_LOW);   // SDA low
         usleep(SLEEP_VALUE);
         }

      // shift the bit mask
      bit_mask >>= 1;

      // clock the data
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
      usleep(SLEEP_VALUE);

      // Set the clock line low
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
      usleep(SLEEP_VALUE);
      }
}

/*********************************************************************************/
// static int read_byte_bus(void);
//
//    Routine to read a byte of data to the serial EEPROM.
//
//      Return Value: Data read from the bus
//
/*********************************************************************************/
static int read_byte_bus(struct occ_handle *occ)
{
   byte I2C_input;
   byte data_from_bus;
   word bit_mask;

   // Set the data line high
   Bit_Outport32(occ, I2CR, I2CR_SDA, I2CR_SDA_HIGH);  // SDA high
   usleep(SLEEP_VALUE);

      // disable the SDA output enable
   Bit_Outport32(occ, I2CR, I2CR_OE, I2CR_OE_LOW);     // OE LOW
   usleep(SLEEP_VALUE);

   // reset the data byte and bit mask
   data_from_bus = 0;
   bit_mask      = 0x80;

   // for each bit
   while (bit_mask >= 1)
      {
      // clock the data
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_HIGH);     // SCL high
      usleep(SLEEP_VALUE);

      // if not zero then add the bit
      I2C_input = (byte)((Inport32(occ, I2CR) & I2CR_SDA_PIN));

      if (I2C_input)
         data_from_bus |= bit_mask;

      // shift the bit mask
      bit_mask >>= 1;

      // Set the clock line low
      Bit_Outport32(occ, I2CR, I2CR_SCL, I2CR_SCL_LOW);   // SCL low
      usleep(SLEEP_VALUE);
      }

   // return the byte read
   return(data_from_bus);
}

/*********************************************************************************/
// int Read_I2C_Bus(byte address, word *data);
//
//       address   - Memory address to read, aligned to even number.
//       offset    - Byte offset from memory address                              
//       data      - Data pointer to put the 2 byte data, even address as MSB, odd as LSB
//
//    Routine to read 2 bytes from the I2C serial bus.                              
//
//                    
//                                                                               
// Return Value:                                                                 
//                                                                               
//    SUCCESS - Read performed successfully.                                     
//    ERROR   - Not able to read from serial bus.                                
//                                                                              
//
/*********************************************************************************/
unsigned int Read_I2C_Bus(struct occ_handle *occ, byte address, byte offset, word *data)
{
   byte temp_data;

   // Set I2C bus to initial state
   Outport32(occ, I2CR, I2CR_DEFAULT);     // all high
   usleep(SLEEP_VALUE);

   // write the start and prepare for a read
   start_signal(occ, address/2);
   write_signal(occ); // set R/W = 0

   // read acknowledge
   if (read_acknowledge(occ) == SUCCESS) {
      // write the byte offset
	  //printf("Writing device offset %d \n",offset);
	  write_byte_bus(occ, offset);
	  
	  if (read_acknowledge(occ) == SUCCESS) {
                start_signal(occ, address/2);
                read_signal(occ); // set R/W = 1

		if (read_acknowledge(occ) == SUCCESS) {
			temp_data = read_byte_bus(occ);
			*data = ((int) temp_data) << 8;
			//printf("data = %x, temp_data = %x \n", *data,temp_data); 

			// send acknowledge
			write_acknowledge(occ);

			// read the second byte
			temp_data = read_byte_bus(occ);
			*data |= temp_data;

			//printf("data = %x, temp_data = %x \n", *data,temp_data); 
	
			// no acknowledge on the last byte		

			// send the stop signal
			stop_signal(occ);
			return(SUCCESS);
		}
		else {
	    		printf("Error: Ack did not go LOW! (memory read address)\n");
			// send the stop signal
			stop_signal(occ);
			return(ERROR);
	  	}
	  }
	  
	  else {
	    printf("Error: Ack did not go LOW! (byte offset)\n");
		// send the stop signal
		stop_signal(occ);
		return(ERROR);
	  }
  
    }

   else {
	   printf("Error: Ack did not go LOW! (memory write address)\n");
      // send the stop signal
      stop_signal(occ);
      return(ERROR);
   }
}

