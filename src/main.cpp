/*
* PackageLicenseDeclared: Apache-2.0
* Copyright (c) 2017 ARM Limited
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "mbed.h"
#include "AT45BlockDevice.h"
#include "FlashIAP.h"
#include "mbed_debug.h"
#include "update_params.h"
#include "FragmentationSha256.h"
#include "BDFile.h"
#include "janpatch.h"
#include "mbed_delta_update.h"

AT45BlockDevice bd;
FlashIAP flash;

static void print_sha256(unsigned char sha_out_buffer[32]) {
    for (size_t ix = 0; ix < 32; ix++) {
        debug("%02x", sha_out_buffer[ix]);
    }
}

void apply_update(BlockDevice* bd, uint32_t bd_offset, size_t bd_size)
{
    flash.init();

    uint32_t addr = POST_APPLICATION_ADDR;
    uint32_t next_sector = addr + flash.get_sector_size(addr);

    const uint32_t page_size = flash.get_sector_size(addr);
    char *page_buffer = new char[page_size];

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
        if (++pkt_counter % 1 == 0 || bd_bytes_to_read == 0) {
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
    debug("\toffset:    %lu\n",   params.offset);
    debug("\tsignature: 0x%x\n",  params.signature);
    debug("\thash:      ");
    print_sha256(params.sha256_hash);
    debug("\n");

    if (err == 0 && params.signature == UpdateParams_t::MAGIC && params.update_pending == 1) {
        debug("Verifying hash...\n");

        uint8_t sha_buffer[528];
        unsigned char sha_out[32];

        FragmentationSha256 sha256(&bd, sha_buffer, sizeof(sha_buffer));
        // first 256 bytes are the RSA/SHA256 signature, ignore those
        sha256.calculate(
            params.offset,
            params.size,
            sha_out);

        bool hash_ok = true;
        for (size_t ix = 0; ix < 32; ix++) {
            if (sha_out[ix] != params.sha256_hash[ix]) {
                hash_ok = false;
                break;
            }
        }

        if (!hash_ok) {
            debug("SHA256 hash did not match. Expected ");
            print_sha256(params.sha256_hash);
            debug(", was ");
            print_sha256(sha_out);
            debug(". Not applying update.\n");
        }
        else {
            debug("SHA256 hash matched. Applying update...\n");

            apply_update(&bd, params.offset, params.size);
        }

        // clear the parameters...
        memset(&params, 0, sizeof(UpdateParams_t));
        bd.program(&params, FOTA_INFO_PAGE * bd.get_read_size(), sizeof(UpdateParams_t));
    }
    else {
        debug("No pending update\n");
    }

    // now copy the current fw. to flash page FOTA_DIFF_OLD_FW_PAGE
    // @todo: how could we know when *not* do to this?
    int total_size = MBED_CONF_APP_TOTAL_FLASH_SIZE - MBED_CONF_APP_BOOTLOADER_SIZE;
    int v;
    v = copy_flash_to_blockdevice(16 * 1024, POST_APPLICATION_ADDR,
        total_size, &bd, FOTA_DIFF_OLD_FW_PAGE * bd.get_read_size());

    if (v != MBED_DELTA_UPDATE_OK) {
        debug("copy_flash_to_blockdevice failed %d\n", v);
    }

    start_app();
}
