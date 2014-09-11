#ifndef OCC_FLASHPROG_H
#define OCC_FLASHPROG_H

/**
 * Class used only for IO exceptions resulting from communications with OCC
 * hardware.
 */
class occ_io_exception {
    public:
        /**
         * Typically, this exception should be thrown by functions which access
         * occlib and get error codes returned from occlib.  The exception
         * is caught and handled in main().  This constructor will simply
         * format a nice error message based on the input values given.
         *
         * @param[in]   func    Function name where error occurred.
         * @param[in]   bar     PCI bar (normally 0)
         * @param[in]   offset  FPGA register offset used when error occurred.
         * @param[in]   return_code This is the return code given by the offending
         *                  function.
         */
        occ_io_exception(std::string const& func, int bar, uint32_t offset, 
                int return_code);
};

/**
 * Reads the contents of flash to a file.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of the flash memory to read
 * @param[in]   file        File name to open for writing contents of flash.
 * @param[in]   num_words   Number of 16-bit words to read.
 * @throws      std::invalid_argument   NULL pointer passed to flash_read
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_read(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file, uint32_t num_words);

/**
 * Verify contents of flash against the contents of a file.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of the flash memory to read
 * @param[in]   file        File name to open for comparison to flash.
 * @throws      std::invalid_argument   NULL pointer passed to flash_read
 * @throws      std::runtime_error  If temporary file tmp.bin fails to delete
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_verify(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file);

/**
 * Erase specified number of blocks of the flash, starting at the given
 * flash word address.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of flash memory to erase.
 * @param[in]   num_blocks  Number of blocks to erase.
 * @throws      std::runtime_error  Wait ready times out during block erase 
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_erase(occ_handle *occ, uint8_t bar, uint32_t flash_addr,
        uint32_t num_blocks);

/**
 * Write contents of file to a given word address of flash.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of flash memory to write.
 * @param[in]   file        File name to open for writing to flash.
 * @throws      std::runtime_error  Wait ready times out during block erase
 * @throws      std::runtime_error  Wait ready times out during data write
 * @throws      std::invalid_argument   Flash addr not aligned to buffer size
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_write(occ_handle *occ, uint8_t bar, uint32_t flash_addr, 
        const char *file);

/**
 * Check if a given block of flash is locked/protected.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of block to test.
 * @return      True if flash is locked/protected.
 * @throws      std::invalid_argument   NULL pointer passed to flash_read
 * @throws      occ_io_exception    In case of OCC communications failure
 */
bool occ_flash_is_block_protected(occ_handle *occ, uint8_t bar, 
        uint32_t flash_addr);

/**
 * Unlock/unprotect a block of flash.
 * 
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @param[in]   flash_addr  Word address of block to unlock/unprotect.
 * @throws      std::runtime_error  If CFI data is invalid, or wait ready times out  
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_block_unprotect(occ_handle *occ, uint8_t bar, uint32_t flash_addr);

/**
 * Verify basic flash communication and leave the flash in an unlocked or
 * unprotected state.
 *
 * @param[in]   occ         Handle to opaque OCC hardware descriptor.
 * @param[in]   bar         PCI bar (normally 0)
 * @throws      std::runtime_error  If CFI data is invalid, or wait ready times out
 * @throws      std::invalid_argument   NULL pointer passed to flash_read
 * @throws      occ_io_exception    In case of OCC communications failure
 */
void occ_flash_init(occ_handle *occ, uint8_t bar);

#endif
