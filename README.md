# mbed OS 5 bootloader for AT45 SPI Flash

Bootloader used for firmware updates over LoRaWAN. Tested with [L-Tek FF1705](https://os.mbed.com/platforms/L-TEK-FF1705/) - based on Multi-Tech xDot.

## How to build

1. Install [mbed CLI](https://github.com/ARMmbed/mbed-cli) and the [GNU ARM Embedded Toolchain](https://launchpad.net/gcc-arm-embedded).
1. Import this project:

    ```
    $ mbed import https://github.com/armmbed/lorawan-fota-bootloader
    ```

1. Build the project:

    ```
    $ mbed compile -m xdot_l151cc -t GCC_ARM --profile ./profiles/release.json
    ```
    
Note that the bootloader size is currently set to `0x7000`, but you can set this lower when building for a release profile if you need more space for your application.

### Debug messages

If you want to see debug messages on the serial port (baud rate 9,600):

1. Build with:

    ```
    $ mbed compile -m xdot_l151cc -t GCC_ARM --profile ./profile/develop.json
    ```

## Acknowledgements

Based on the initial work by Chris Snow.

