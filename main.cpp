#include "mbed.h"
#include "AT45BlockDevice.h"
#include "FlashIAP.h"
#include "FATFileSystem.h"
#include "debug.h"
#include "FragmentationCrc64.h"

// These values need to be the same between target application and bootloader!
#define     FOTA_INFO_PAGE         0x1800    // The information page for the firmware update
#define     FOTA_UPDATE_PAGE       0x1801    // The update starts at this page (and then continues)

AT45BlockDevice bd;
FlashIAP flash;

struct UpdateParams_t {
    bool update_pending;
    size_t size;
    uint32_t signature;
    uint64_t hash;

    static const uint32_t MAGIC = 0x1BEAC000;
};

void apply_update(BlockDevice* bd, uint32_t bd_offset, size_t bd_size)
{
    flash.init();

    const uint32_t page_size = flash.get_page_size();
    char *page_buffer = new char[page_size];

    uint32_t addr = POST_APPLICATION_ADDR;
    uint32_t next_sector = addr + flash.get_sector_size(addr);
    bool sector_erased = false;

    size_t pkt_counter = 0;

    size_t bd_bytes_to_read = bd_size;

    while (bd_bytes_to_read > 0) {

        // Determine how much to read
        size_t length = page_size;
        if (length > bd_bytes_to_read) length = bd_bytes_to_read;

        // Read data for the current page
        memset(page_buffer, 0, sizeof(page_buffer));
        bd->read(page_buffer, bd_offset, length);

        // Erase this page if it hasn't been erased
        if (!sector_erased) {
            flash.erase(addr, flash.get_sector_size(addr));
            sector_erased = true;
        }

        // Program page (always needs page_size length, but it's OK, the page is already cleared)
        flash.program(page_buffer, addr, page_size);

        addr += length;
        bd_offset += length;
        bd_bytes_to_read -= length;

        if (addr >= next_sector) {
            next_sector = addr + flash.get_sector_size(addr);
            sector_erased = false;
        }

        // progress message
        if (++pkt_counter % 5 == 0 || bd_bytes_to_read == 0) {
            debug("Flashing: %d%% (%lu / %lu bytes)\n",
                static_cast<int>((1.0f - static_cast<float>(bd_bytes_to_read) / static_cast<float>(bd_size)) * 100.0f),
                bd_size - bd_bytes_to_read, bd_size);
        }
    }
    delete[] page_buffer;

    flash.deinit();
}

int start_app() {
    debug("Starting the application at %p\n", POST_APPLICATION_ADDR);

    bd.deinit();

    mbed_start_application(POST_APPLICATION_ADDR);

    return 0;
}

int main() {
    debug("Hello from the bootloader\n");

    int err;
    if ((err = bd.init()) != 0) {
        debug("Could not initialize block device (%d)...\n", err);
        return start_app();
    }

    // read info on page FOTA_INFO_PAGE to see if there's an update
    UpdateParams_t params;
    err = bd.read(&params, FOTA_INFO_PAGE * bd.get_read_size(), sizeof(UpdateParams_t));

    debug("Update parameters:\n");
    debug("\terr:       %d\n",    err);
    debug("\tpending:   %d\n",    params.update_pending);
    debug("\tsize:      %lu\n",   params.size);
    debug("\tsignature: 0x%x\n",  params.signature);
    // nanolib printf behavior is different from non-nanolib
    uint8_t* hash = (uint8_t*)(&params.hash);
    debug("\thash:      %02x%02x%02x%02x%02x%02x%02x%02x\n", hash[7], hash[6], hash[5], hash[4], hash[3], hash[2], hash[1], hash[0]);

    if (err == 0 && params.signature == UpdateParams_t::MAGIC && params.update_pending == 1) {
        debug("Verifying hash...\n");

        uint8_t crc_buffer[528];

        FragmentationCrc64 crc64(&bd, crc_buffer, sizeof(crc_buffer));
        uint64_t crc_res = crc64.calculate(FOTA_UPDATE_PAGE * bd.get_read_size(), params.size);

        if (crc_res != params.hash) {
            uint8_t* crc = (uint8_t*)(&crc_res);
            debug("CRC64 hash did not match. Expected %02x%02x%02x%02x%02x%02x%02x%02x, was %02x%02x%02x%02x%02x%02x%02x%02x. Not applying update.\n",
                hash[7], hash[6], hash[5], hash[4], hash[3], hash[2], hash[1], hash[0],
                crc[7],  crc[6],  crc[5],  crc[4],  crc[3],  crc[2],  crc[1],  crc[0]);
        }
        else {
            debug("CRC64 hash matched. Applying update...\n");

            // update starts at page FOTA_UPDATE_PAGE
            apply_update(&bd, FOTA_UPDATE_PAGE * bd.get_read_size(), params.size);
        }

        // clear the parameters...
        memset(&params, 0, sizeof(UpdateParams_t));
        bd.program(&params, FOTA_INFO_PAGE * bd.get_read_size(), sizeof(UpdateParams_t));
    }
    else {
        debug("No pending update\n");
    }

    start_app();
}
