/* See LICENSE file for license details */

/* Standard library includes */

#include <cstdio>
#include <cstdint>

#include "MiniFB.h"

extern "C" {
#include "kiwi/megadrive.h"
}

uint8_t ROM[0x400000];

uint16_t SCREEN[224][320];
bool reboot = false;


void emulate() {
    while (!reboot) {
        frame();

        if (mfb_update(SCREEN, 50) == -1)
            reboot = true;

    }
    reboot = false;
}

void readfile(const char* pathname, uint8_t* dst) {
    set_rom(ROM, 0x400000);
    FILE* file = fopen(pathname, "rb");
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    fread(dst, sizeof(uint8_t), size, file);
}

int main(int argc, char** argv) {
    readfile(argv[1], ROM);

    if (!mfb_open("sega", 320, 224, 4))
        return 0;

    vdp_set_buffers((unsigned char*)SCREEN);

    emulate();
}
