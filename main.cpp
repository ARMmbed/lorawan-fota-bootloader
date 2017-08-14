#include "mbed.h"
#include "AT45BlockDevice.h"
#include "FlashIAP.h"
#include "FATFileSystem.h"

#if defined(NDEBUG) && NDEBUG == 1
#define debug(...) do {} while(0)
#else
#define debug(...) printf(__VA_ARGS__)
#endif

AT45BlockDevice bd;
FlashIAP flash;

struct UpdateParams_t {
    bool update_pending;
    size_t size;
    uint32_t signature;

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

        // Program page
        flash.program(page_buffer, addr, length);

        addr += length;
        bd_offset += length;
        bd_bytes_to_read -= length;

        if (addr >= next_sector) {
            next_sector = addr + flash.get_sector_size(addr);
            sector_erased = false;
        }
    }
    delete[] page_buffer;

    flash.deinit();
}

int start_app() {
    debug("Starting the application at %p\n", POST_APPLICATION_ADDR);

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

    // read info on page 0x1800 to see if there's an update
    UpdateParams_t params;
    err = bd.read(&params, 0x1800 * bd.get_read_size(), sizeof(UpdateParams_t));

    if (err == 0 && params.signature == UpdateParams_t::MAGIC && params.update_pending == 1) {
        debug("I has update pending, size=%lu bytes!\n", params.size);

        // update starts at page 0x1801
        apply_update(&bd, 0x1801 * bd.get_read_size(), params.size);
    }
    else {
        debug("No pending update. err=%d, signature=%x, update_pending=%d\n",
            err, params.signature, params.update_pending);
    }

    start_app();
}
