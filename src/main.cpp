/* See LICENSE file for license details */

/* Standard library includes */
#include <cstdio>
#include <cstdint>


extern "C" {
#include "kiwi/megadrive.h"
}

#if !PICO_ON_DEVICE
#include "MiniFB.h"
uint8_t ROM[0x400000];
uint16_t SCREEN[224][320];
#else
#include "pico/platform.h"
#include <hardware/flash.h>

#include "graphics.h"

#define HOME_DIR "\\SEGA"
extern char __flash_binary_end;
#define FLASH_TARGET_OFFSET (((((uintptr_t)&__flash_binary_end - XIP_BASE) / FLASH_SECTOR_SIZE) + 1) * FLASH_SECTOR_SIZE)
uintptr_t rom = XIP_BASE + FLASH_TARGET_OFFSET;

uint8_t SCREEN[224][320];
#endif

bool reboot = false;


void emulate() {
    while (!reboot) {
        frame();
#if !PICO_ON_DEVICE
        if (mfb_update(SCREEN, 60) == -1)
            reboot = true;
#endif
    }
    reboot = false;
}

#if !PICO_ON_DEVICE
void readfile(const char* pathname, uint8_t* dst) {
    set_rom(ROM, 0x400000);
    FILE* file = fopen(pathname, "rb");
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    fread(dst, sizeof(uint8_t), size, file);
}
#endif

int main(int argc, char** argv) {

#if !PICO_ON_DEVICE
    readfile(argv[1], ROM);

    if (!mfb_open("sega", 320, 224, 4))
        return 0;
#endif

    vdp_set_buffers((unsigned char*)SCREEN);

    emulate();
}
