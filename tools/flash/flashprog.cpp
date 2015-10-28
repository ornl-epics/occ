/**
 * [06/2014]
 *
 * Flash programming app specifically designed for Micron StrataFlash Embedded
 * Memory (PC28F512G18xx).  
 *
 * This app could be extended to support other Micron StrataFlash devices
 * and sizes, but was not designed specifically with more general purposes in
 * mind.
 *
 * It would also be recommended to apply a class hierarchy in which a Micron
 * Strata Flash programmer class could descend from a more general flash 
 * programmer class.  We could also abstract away the FPGA flash access
 * mechanism implemented in flash_read()/flash_write().
 *
 * Note: the concept of "section" is a logical construction of the developer.
 * We divide the device into 4 sections.  On 512Mbit flash, each section is
 * therefore 16MB.  This is an appropriate choice because the typical 
 * programming file for the initial use case is 11MB, so we desire to erase and 
 * program a "section".  The underlying flash interface as defined in the header
 * is made more generic, accepting word addresses to flash, instead of mythical
 * "sections".
 *
 * Note: To simplify callers, most functions are designed to return void, and
 * throw exceptions to indicate critical errors.
 *
 * Functional interface documentation provided in occ_flashprog.h, using Doxygen
 * syntax.  Static entities are documented herein.
 *
 * Greg Guyotte <guyottegs@ornl.gov>
 *
 * 10/16/2015: READ CFI support extended to collect flash geometry.  This allows
 * support for a wider range of Micron devices, including the MT28GU01GAAA1EGC,
 * which is a 1Gb part that is now on the OCC.
 */

#include <occlib_hw.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <boost/iostreams/device/mapped_file.hpp>
#include <iomanip>
#include <time.h>
#include "flashprog.h"
namespace io = boost::iostreams;
using namespace std;

/* Locally useful defines */
#define CMD_WRITE  1
#define CMD_READ   2
#define CMD_VERIFY 3
#define CMD_ERASE  4
#define CMD_PROGRAM 5
#define DELAY_1_S  (DELAY_1000_MS)
#define DELAY_1000_MS (1000)
#define DELAY_100_MS  (100)
#define PROGRESS_BAR_WIDTH (30)
#define BYTES_PER_WORD (2)
#define NUM_PARTITIONS (8)

/* Global flash geometry data */
uint32_t gbl_flash_size_bytes;
uint32_t gbl_flash_size_words;
uint32_t gbl_section_size_bytes;
uint32_t gbl_block_size_bytes;
uint32_t gbl_section_size_words;
uint32_t gbl_partition_size_words;
uint32_t gbl_block_size_words;
uint32_t gbl_buffered_pgm_sz;

/* StrataFlash Command Set */
#define PGM_READ_CFG_REG_SETUP            (0x0060)
#define PGM_READ_CFG_REG_CONFIRM          (0x0003)
#define PGM_EXT_CFG_REG_SETUP             (0x0060)
#define PGM_EXT_CFG_REG_CONFIRM           (0x0004)
#define PGM_OTP_AREA                      (0x00C0)
#define CLEAR_STS_REG                     (0x0050)
#define READ_ARRAY                        (0x00FF)
#define READ_STS_REG                      (0x0070)
#define READ_ID                           (0x0090)
#define READ_CFI                          (0x0098)
#define SINGLE_WORD_PGM                   (0x0041)
#define BUFFERED_PGM_SETUP                (0x00E9)
#define BUFFERED_PGM_CONFIRM              (0x00D0)
#define BUFFERED_ENH_FACTORY_PGM_SETUP    (0x0080)
#define BUFFERED_ENH_FACTORY_PGM_CONFIRM  (0x00D0)
#define BLOCK_ERASE_SETUP                 (0x0020)
#define BLOCK_ERASE_CONFIRM               (0x00D0)
#define LOCK_BLOCK_SETUP                  (0x0060)
#define LOCK_BLOCK_CONFIRM                (0x0001)
#define UNLOCK_BLOCK_SETUP                (0x0060)
#define UNLOCK_BLOCK_CONFIRM              (0x00D0)
#define LOCK_DOWN_BLOCK_SETUP             (0x0060)
#define LOCK_DOWN_BLOCK_CONFIRM           (0x002F)
#define SUSPEND                           (0x00B0)
#define RESUME                            (0x00D0)
#define BLANK_CHECK_SETUP                 (0x00BC)
#define BLANK_CHECK_CONFIRM               (0x00D0)

/* Strata Flash useful register masks */
#define DEVICE_READY                      (0x80)
#define STATUS_REG_ERROR_MASK             (0x37F)

/* FPGA Registers and masks */
#define FLASH_CONTROL_REG                 (0x0120)
#define FLASH_DATA_REG                    (0x0124)
#define FLASH_WRITE_OPERATION             (0x1<<27)
#define FLASH_READ_OPERATION              (0x1<<26)
#define FLASH_DATA_MASK                   (0xFFFF)

/* ======================= STATIC FUNCTIONS ================================ */

/* lookup table for bit_flip() */
static const uint8_t lookup[16] = {
   0x0, 0x8, 0x4, 0xC,
   0x2, 0xA, 0x6, 0xE,
   0x1, 0x9, 0x5, 0xD,
   0x3, 0xB, 0x7, 0xF };

/**
 * Reverse the order of bits within a byte.  Uses lookup table.
 *
 * @param[in]   n   Input byte
 * @return    Output byte, bit flipped.
 */
static uint8_t bit_flip( uint8_t n )
{
    return (lookup[n&0x0F] << 4) | lookup[n>>4];
}

/**
 * Produces an attractive progress bar for monitoring status of long operations
 * which process a number of bytes.
 *
 * @param[in]   x   Progress towards goal
 * @param[in]   n   Integer representing completion of task, where x <= n.
 * @param[in]   w   Width of progress bar in characters.  30 looks pretty good.
 * @param[in]   bytes   This integer value is displayed first, showing progress
 *                      to user in bytes.  This is useful to show incremental 
 *                      progress for extremely long tasks, where it may take
 *                      several seconds for even 1% point of progress to occur.
 */
static inline void loadbar(uint32_t x, uint32_t n, uint32_t w, uint32_t bytes)
{
    if ( (x != n) && (x % (n/300+1) != 0) ) return;
 
    float ratio  =  x/(float)n;
    int   c      =  ratio * w;
 
    cout << dec << "(bytes " << setw(8) << bytes << ")  ";
    cout << setw(3) << (int)(ratio*100) << "% [";
    for (int x=0; x<c; x++) cout << "=";
    for (int x=c; x<(int)w; x++) cout << " ";
    cout << "]\r" << flush;
}

/**
 * Print a given string to stdout, centered.  The fill character will be used to
 * pad any excess field width to the left and right of the given string.
 *
 * @param   fillc   Character used for filling the field.
 * @param   width   Overall width of the desired text field.
 * @param   text    Text to be printed
 */
static void centered_output(char fillc, uint8_t width, const char *text) {
    int text_width;

    cout.fill(fillc);
    text_width = strlen(text);
    cout << setw(width - text_width) << text;
    cout << setw(text_width) << fillc << endl;
    cout.fill(' ');
}

/**
 * Sleep for a given number of nanoseconds.
 *
 * @param[in]   ns  Number of nanoseconds to sleep.
 */
static void nsleep(uint32_t ns) {
    timespec tS, tS2;
    uint32_t ret;

    ret = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tS);
    if (ret != 0)
        throw std::runtime_error("clock_gettime returned failure");
    while (1) {
        ret = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tS2);
        if (ret != 0)
            throw std::runtime_error("clock_gettime returned failure");
        if (tS2.tv_sec != tS.tv_sec)
            break;        
        if ((tS2.tv_nsec - tS.tv_nsec) > ns)
            break;
    }  
}

/**
 * Print usage for program.
 *
 * @param[in]   progname    Pointer to char string with the program name.
 */
static void usage(const char *progname) {
    printf("Usage: %s -d <device file> -s <section> \n\
                       {-r <f> | -w <f> | -v <f> | -p <f> |-e}\n", progname);
    printf("\n");
    printf("Section and device-file are required.\n");
    printf("Choose only one of the read|write|verify|erase operations.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -d, --device-file FILE   Full path to OCC board device file\n");
    printf("  -s, --section 0..3       Select 16MB flash section to use\n");
    printf("  -r, --read FILE          Read from flash into file\n");
    printf("  -w, --write FILE         Write binary file to flash\n");
    printf("  -v, --verify FILE        Verify flash contents against file\n");
    printf("  -e, --erase              Erases selected section of flash\n");
    printf("  -p, --program FILE       Combines write and verify\n");
    printf("\n");
}

/**
 * Abstract flash write access via occlib.  Used to write both commands and 
 * data.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  This word addr placed on the flash memory addr bus
 * @param[in]   flash_data  Data written to the flash memory data bus 
 *              (used for both flash commands and write data)
 */
static void flash_write(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        uint32_t flash_data) {
    int ret = -1;

    flash_addr = flash_addr | FLASH_WRITE_OPERATION;
    flash_data = flash_data & FLASH_DATA_MASK;
  
    /* write data to Flash Data Register */
    if (occ_io_write(occ, bar, FLASH_DATA_REG, &flash_data, 1) < 0)
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_DATA_REG, ret);
    }


    /* write address and operation bit to Flash Control Register */  
    ret = occ_io_write(occ, bar, FLASH_CONTROL_REG, &flash_addr, 1);
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_CONTROL_REG, ret);
    }
    
    /* We must clear the write bit manually for two reasons.  (1) the hardware 
     * doesn't autoclear the bit.  (2) the hardware requires a rising edge to
     * trigger the operation.
     */
    flash_addr &= !FLASH_WRITE_OPERATION;
    ret = occ_io_write(occ, bar, FLASH_CONTROL_REG, &flash_addr, 1);
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_CONTROL_REG, ret);
    }

    /** 
     * @bug A small delay is necessary between writes to avoid status reg errors
     * when programming.  (The reason this delay is required is still unknown)
     * Testing found that usleep(1) actually induces a delay of about 70uS on
     * the systems used for testing.  usleep(1) will work here, but causes
     * unnecessarily slow programming phase.  The current implementation has
     * been shown to reliably give a delay of about 3uS.
     */
    nsleep(2500); 
}

/**
 * Abstract flash read access via occlib.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  This word addr placed on the flash memory addr bus
 * @param[out]  flash_data  Pointer to read data coming from the flash
 */
static void flash_read(occ_handle *occ, uint8_t bar, uint32_t flash_addr,
        uint32_t *flash_data) {
    int ret = -1;

    if (flash_data == 0)
        throw (std::invalid_argument("flash_data is NULL pointer"));

    flash_addr = flash_addr | FLASH_READ_OPERATION;
        
    /* write address and operation bit to Flash Control Register */  
    ret = occ_io_write(occ, bar, FLASH_CONTROL_REG, &flash_addr, 1);
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_CONTROL_REG, ret);
    } 

    /* read data from Flash Data Register */
    ret = occ_io_read(occ, bar, FLASH_DATA_REG, flash_data, 1);
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_DATA_REG, ret); 
    }

    /* We must clear the read bit manually for two reasons.  (1) the hardware 
     * doesn't autoclear the bit.  (2) the hardware requires a rising edge to
     * trigger the operation.
     */
    flash_addr &= !FLASH_READ_OPERATION;
    ret = occ_io_write(occ, bar, FLASH_CONTROL_REG, &flash_addr, 1);
    if (ret < 0) {
        throw occ_io_exception(__func__, bar, FLASH_CONTROL_REG, ret);
    }
}

/**
 * Read flash status register until device is ready, or until provided timeout
 * has expired.  If necessary this function can put the flash into the read
 * status state.  Will print an error if any error bits are set in the status
 * register, but leaves it up to the caller to check flash_data and determine
 * what (if anything) to do about the error.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Pointer to word addressed flash memory
 * @param[out]  flash_data  Pointer to contents of flash status register
 * @param[in]   timeout_ms  Timeout value given in milliseconds.
 * @param[in]   force_read_sts   If true, put flash in read status state
 *                  first.  This is often not necessary because many flash
 *                  commands automatically put the flash into read status state.
 */
static void flash_wait_ready(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        uint32_t *flash_data, uint32_t timeout_ms, bool force_read_sts) {
    uint32_t timeout_us = timeout_ms*1000;

    if (force_read_sts) {
        flash_write(occ, bar, flash_addr, READ_STS_REG);
        flash_read(occ, bar, flash_addr, flash_data);
    }

    do {
        flash_read(occ, bar, flash_addr, flash_data);
        if (*flash_data & DEVICE_READY)
            break;

        /* testing proved that 1us worked reasonably well */
        usleep(1);
    } while (--timeout_us);

    if (*flash_data & STATUS_REG_ERROR_MASK) {
        /* report status register errors and clear them */
        flash_write(occ, bar, flash_addr, CLEAR_STS_REG);
        cout << "Status reg error = " << *flash_data << "; Flash addr = " 
            << flash_addr << endl;
    }

    if (timeout_us == 0) 
        throw std::runtime_error("Flash wait ready timed out");
}

/* ======================= PUBLIC FUNCTIONS ================================ */
/* ================== (documentation in header) ============================ */

occ_io_exception::occ_io_exception(std::string const& func, int bar, 
        uint32_t offset, int return_code) {
    cerr << "occ_io_exception in " << func << "(): cannot access bar" << bar 
        << " at offset 0x" << hex << offset << "(return code " << return_code 
        << ")" << endl;
}

void occ_flash_read(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file, uint32_t num_words) {
    uint32_t word_index=0;
    ofstream read_file;
    union {
        char char_data [sizeof(uint32_t)];
        uint32_t int_data;
        uint8_t byte_array[sizeof(uint32_t)];
    };

    read_file.open(file, ios::binary);

    cout << endl << "Reading flash at addr 0x" <<hex<<flash_addr << ":" << endl;
 
    for (word_index=0; word_index < num_words; word_index++) {
        if (((flash_addr+word_index) % gbl_partition_size_words) == 0) {
            /* put flash in Read Array mode at each partition boundary */
            flash_write(occ, bar, flash_addr+word_index, READ_ARRAY);
        }
        flash_read(occ, bar, flash_addr+word_index, &int_data);
        /* Per XAPP470 (App note for the Series 7 FPGAs from Xilinx), each byte 
         * must be bit swapped prior on read of flash.
         */
        byte_array[0] = bit_flip(byte_array[0]);
        byte_array[1] = bit_flip(byte_array[1]);
        read_file.write(&char_data[1], sizeof(char));
        read_file.write(&char_data[0], sizeof(char));
  
        loadbar(word_index+1, num_words, PROGRESS_BAR_WIDTH, (word_index+1)*2);
    }
    
    cout << endl << "Flash read complete." << endl << endl;
    read_file.close();
}

void occ_flash_verify(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file) {
    cout << endl << "Reading input file " << file << "...";
    /* using memory mapped files for speed in verify */
    io::mapped_file_source f2(file);
    
    /* read contents of flash up to the size of the input file */
    occ_flash_read(occ, bar, flash_addr, "tmp.bin", f2.size()/2);
    io::mapped_file_source f1("tmp.bin");

    cout << "Comparing files..." << endl;
    if (f1.size() == f2.size()
        && std::equal(f1.data(), f1.data() + f1.size(), f2.data()))
        cout << "  The flash contents are identical to input file\n";
    else
        cout << "  The file contents differ from input file\n";

    if (remove( "tmp.bin" ) != 0 )
        throw std::runtime_error("Can't delete temporary verify file");
   }

void occ_flash_erase(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        uint32_t num_blocks) {
    uint32_t status_reg, current_flash_addr, block;

    cout << endl << "Erasing flash at addr 0x" <<hex<<flash_addr<< ":" << endl;

    /* flash memory is erased by blocks */
    for (block=0; block<num_blocks; block++) {
        current_flash_addr = (block*gbl_block_size_words) + flash_addr;

        /* setup and confirm the ERASE command */
        flash_write(occ, bar, current_flash_addr, BLOCK_ERASE_SETUP);
        flash_write(occ, bar, current_flash_addr, BLOCK_ERASE_CONFIRM);
               
        /* wait for block erase to complete (0.9 typ) */
        flash_wait_ready(occ, bar, current_flash_addr, &status_reg, 
                DELAY_1_S*2, false);
        if (status_reg & STATUS_REG_ERROR_MASK)
            throw std::runtime_error("Block erase error");

        loadbar(block+1, num_blocks, PROGRESS_BAR_WIDTH, 
                (block+1)*gbl_block_size_bytes);
    }
    cout << endl << "Erased " << num_blocks << " blocks." << endl << endl;
}

void occ_flash_write(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file) {
    uint32_t erase_blocks, status_reg, file_len, byte_index, bytes_written=0,
        chunk_count=0, chunk_index=0, flash_data;
    unsigned char write_data[gbl_buffered_pgm_sz];
    ifstream myfile;

    /* input addresses should be aligned to the buffer size, for optimal
     * performance, and to assure that a buffer write never crosses a 
     * block boundary
     */
    if (flash_addr % gbl_buffered_pgm_sz)
        throw std::invalid_argument("Flash addr not aligned to buffer size");

    /* get length of file and # of 1KB chunks to program (for progress bar) */
    myfile.open(file, ios::in|ios::binary);
    myfile.seekg(0, myfile.end);
    file_len = myfile.tellg();
    myfile.seekg(0, myfile.beg);
    chunk_count = (file_len/gbl_buffered_pgm_sz) + !!(file_len%gbl_buffered_pgm_sz);
      
    /* it is required to erase flash prior to writing */
    erase_blocks = (file_len/gbl_block_size_bytes) + !!(file_len%gbl_block_size_bytes);
    occ_flash_erase(occ, bar, flash_addr, erase_blocks);

    cout << "Programming flash at addr 0x" << hex << flash_addr << ":" << endl;

    for (chunk_index=0; chunk_index < chunk_count; chunk_index++) {
        /* flash must be programmed in small chunks, limit of flash device */
        myfile.read((char *)write_data, gbl_buffered_pgm_sz);

        /* write setup command to flash for buffered programming */
        flash_write(occ, bar, flash_addr, BUFFERED_PGM_SETUP);
        
        /* write word count - 1 as required by flash device */
        flash_write(occ, bar, flash_addr, (myfile.gcount()/2)-1);

        /* write the data chunk to the flash buffer */
        for (byte_index=0; byte_index<myfile.gcount(); byte_index+=2) {
            /* Per XAPP470 (App note for the Series 7 FPGAs from Xilinx), each 
             * byte must be bit swapped when written to flash.
             */
            write_data[byte_index] = bit_flip(write_data[byte_index]);
            write_data[byte_index+1] = bit_flip(write_data[byte_index+1]);
            flash_data = write_data[byte_index+1] | (write_data[byte_index]<<8);
            flash_write(occ, bar, flash_addr+(byte_index/2), flash_data);
            bytes_written += 2;
        }
       
        /* issue confirm, which causes flash to write buffer to flash memory */
        flash_write(occ, bar, flash_addr, BUFFERED_PGM_CONFIRM);

        flash_wait_ready(occ, bar, flash_addr, &status_reg, DELAY_1_S*5, false);
        if (status_reg & STATUS_REG_ERROR_MASK)
            throw std::runtime_error("Flash write error");

        flash_addr += (byte_index/2);
        loadbar(chunk_index+1, chunk_count, PROGRESS_BAR_WIDTH, bytes_written);
    }

    cout << endl << "Flash programming complete." << endl << endl;

    myfile.close();
}

bool occ_flash_is_block_protected(occ_handle *occ, uint8_t bar, 
        uint32_t flash_addr) {
    uint32_t block_lock_status;

    flash_write(occ, bar, flash_addr, READ_ID);
    flash_read(occ, bar, flash_addr + 0x2, &block_lock_status);
  
    if (block_lock_status & 0x1)
        return true;
    else
        return false;
}

void occ_flash_block_unprotect(occ_handle *occ, uint8_t bar, 
        uint32_t flash_addr) {
    uint32_t status_reg;

    /* send setup and confirm commands */
    flash_write(occ, bar, flash_addr, UNLOCK_BLOCK_SETUP);
    flash_write(occ, bar, flash_addr, UNLOCK_BLOCK_CONFIRM);

    flash_wait_ready(occ, bar, flash_addr, &status_reg, DELAY_1_S*2, false);
    if (status_reg & STATUS_REG_ERROR_MASK)
        throw std::runtime_error("Block unprotect error");

    if (occ_flash_is_block_protected(occ, bar, flash_addr))
        throw std::runtime_error("Could not unlock block");
}

void occ_flash_init(occ_handle *occ, uint8_t bar) {
    uint32_t cfi[5], block, flash_addr, status_reg, device_size_bits=0,
        write_buffer_bits=0, num_erase_regions=0, erase_block_size_lsb=0,
        erase_block_size_msb=0;
    
    /* clear status register and send READ_CFI command for address 0 */
    flash_write(occ, bar, 0, CLEAR_STS_REG);
    flash_write(occ, bar, 0, READ_CFI);

    /*
     * Init StrataFlash Geometry Parameters from CFI output.
     * The flash is word addressed, each word being 2 bytes long.
     * For programming purposes, we arbitrarily divide the device into
     * 4 sections.
     */ 
    flash_read(occ, bar, 0x27, &device_size_bits);
    flash_read(occ, bar, 0x2A, &write_buffer_bits);
    flash_read(occ, bar, 0x2C, &num_erase_regions);
    flash_read(occ, bar, 0x2F, &erase_block_size_lsb);
    flash_read(occ, bar, 0x30, &erase_block_size_msb);

    cout << "Flash geometry from CFI:" << endl;
    cout << "  device size bits = " << device_size_bits << endl;
    cout << "  write buffer bits = " << write_buffer_bits << endl;
    cout << "  num erase regions = " << num_erase_regions << endl;
    cout << "  erase block size LSB = " << erase_block_size_lsb << endl;
    cout << "  erase block size MSB = " << erase_block_size_msb << endl;

    gbl_flash_size_bytes = (1 << device_size_bits);
    gbl_flash_size_words = gbl_flash_size_bytes / BYTES_PER_WORD;
    gbl_section_size_bytes = gbl_flash_size_bytes / 4;
    gbl_block_size_bytes = 256 * ((erase_block_size_msb << 8) | (erase_block_size_lsb));
    gbl_section_size_words = gbl_section_size_bytes / BYTES_PER_WORD;
    gbl_partition_size_words = gbl_flash_size_words / NUM_PARTITIONS;
    gbl_block_size_words = gbl_block_size_bytes / BYTES_PER_WORD; 
    gbl_buffered_pgm_sz = (1 << write_buffer_bits);

    for (block=0; block<(gbl_flash_size_bytes/gbl_block_size_bytes); block++) {
        flash_addr = (block * gbl_block_size_words);
        /* clear status register */
        flash_write(occ, bar, flash_addr, CLEAR_STS_REG);
        
        /* at partition boundaries, check the CFI data */
        if ((flash_addr % gbl_partition_size_words) == 0) {
            flash_write(occ, bar, flash_addr, READ_CFI);

            memset(cfi, 0, 5 * sizeof(uint32_t));

            /* read out 5 words of CFI data for a sanity check */
            flash_read(occ, bar, 0x10, &cfi[0]);
            flash_read(occ, bar, 0x11, &cfi[1]);
            flash_read(occ, bar, 0x12, &cfi[2]);
            flash_read(occ, bar, 0x13, &cfi[3]);
            flash_read(occ, bar, 0x14, &cfi[4]);
                      
            if ((cfi[0] != 0x51) || (cfi[1] != 0x52) || (cfi[2] != 0x59) || 
                (cfi[3] != 0x0) || (cfi[4] != 0x2)) {
                cout << "CFI error, flash_addr = " << flash_addr << endl;
                cout << "  cfi data= " << cfi[0] << " " << cfi[1] << " " 
                    << cfi[2] << cfi[3] << " " << cfi[4] << endl;

                flash_wait_ready(occ, bar, flash_addr, &status_reg, 
                        DELAY_1_S*2, true);
             
                throw std::runtime_error("CFI data mismatch");
            }
        }
        occ_flash_block_unprotect(occ, bar, flash_addr);
    }
}

int main(int argc, char **argv) {
    const char *device_file = NULL, *input_file = NULL;
    struct occ_handle *occ;
    uint32_t command = 0, section = -1, flash_addr;
    uint8_t bar = 0;

    for (int i = 1; i < argc; i++) {
        const char *key = argv[i];

        if (strncmp(key, "-h", 2) == 0 || strncmp(key, "--help", 6) == 0) {
            usage(argv[0]);
            return 1;
        }
        if (strncmp(key, "-d", 2) == 0 || 
            strncmp(key, "--device-file", 13) == 0) {
            if ((i + 1) >= argc)
                break;
            device_file = argv[++i];
        }
        if (strncmp(key, "-r", 2) == 0 || strncmp(key, "--read", 6) == 0) {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }
            command = CMD_READ;
            input_file = argv[++i];
        }
        if (strncmp(key, "-w", 2) == 0 || strncmp(key, "--write", 7) == 0) {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }
            input_file = argv[++i];
            command = CMD_WRITE;
        }
        if (strncmp(key, "-v", 2) == 0 || strncmp(key, "--verify", 8) == 0) {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }
            command = CMD_VERIFY;
            input_file = argv[++i];
        }
        if (strncmp(key, "-p", 2) == 0 || strncmp(key, "--program", 9) == 0) {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }
            command = CMD_PROGRAM;
            input_file = argv[++i];
        }
        if (strncmp(key, "-s", 2) == 0 || strncmp(key, "--section", 9) == 0) {
            if ((i + 1) >= argc)
                break;
            section = strtol(argv[++i], NULL, 0);
        }
        if (strncmp(key, "-e", 2) == 0 || strncmp(key, "--erase", 7) == 0) {
            command = CMD_ERASE;
        }
    }

    /* We always require device_file, valid section, and some command.
     * If not erasing, we also require an input_file 
     */
    if (device_file == NULL || ((command != CMD_ERASE) && (input_file == NULL)) 
            || (section == (uint32_t)-1)  || (section > 3) || (command == 0)) {
        usage(argv[0]);
        return 1;
    }

    try {
        if (occ_open(device_file, OCC_INTERFACE_OPTICAL, &occ) != 0) {
            fprintf(stderr, "ERROR: cannot initialize OCC interface\n");
            return 3;
        }

        occ_flash_init(occ, bar);
 
        flash_addr = section * gbl_section_size_words;

        switch (command) {
            case CMD_WRITE:    
                occ_flash_write(occ, bar, flash_addr, input_file);
                break;
            case CMD_READ:
                occ_flash_read(occ, bar, flash_addr, input_file, 
                        gbl_section_size_words);
                break;
            case CMD_VERIFY:
                occ_flash_verify(occ, bar, flash_addr, input_file);
                break;
            case CMD_ERASE:
                /* in this program we erase an entire "section" */
                occ_flash_erase(occ, bar, flash_addr, 
                        gbl_section_size_bytes/gbl_block_size_bytes);
                break;
            case CMD_PROGRAM:
                /* use some formatted output to delineate the two steps */
                centered_output('-', 55, " PROGRAMMING PHASE ");
                occ_flash_write(occ, bar, flash_addr, input_file);
                centered_output('-', 55, " VERIFICATION_PHASE ");
                occ_flash_verify(occ, bar, flash_addr, input_file);
                break;
            default:
                cerr << "Bad command, exiting" << endl;
                return 1;
        }

        occ_close(occ);
    }
    catch (occ_io_exception ex) {
        return 2;
    }
    catch (std::invalid_argument const& ex) {
        cout << ex.what() << endl;
        return 2;
    }
    catch (std::out_of_range const& ex) {
        cout << ex.what() << endl;
        return 2;
    }
    catch (std::runtime_error const& ex) {
        cout << ex.what() << endl;
        return 2;
    }
    catch (...) {
        cout << "Unknown exception" << endl;
        return 2;
    }

    return 0;
}
