#pragma GCC optimize("Ofast")
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "m68k/m68k.h"
#include "vdp.h"

#if PICO_ON_DEVICE
#include <pico/platform.h>
#endif

#include "stdint.h"

/*
 * Megadrive VDP emulation
 */

uint8_t VRAM[0x10000];
uint16_t CRAM[0x40];
uint16_t VSRAM[0x40];
uint8_t vdp_reg[0x20];

#if PICO_ON_DEVICE
uint8_t *screen;
#include <graphics.h>
#else
uint16_t *screen;
#endif

int control_code = 0;
unsigned int control_address = 0;
int control_pending = 0;
unsigned int vdp_status = 0x3400;

int screen_width;
int screen_height;

int dma_length;
unsigned int dma_source;
int dma_fill = 0;
#define RGB888(r, g, b) ((r<<16) | (g << 8 ) | b )
#define BITS(v, idx, n) (((v) >> (idx)) & ((1 << (n)) - 1))
#define COLOR_3B_TO_8B(c)  (((c) << 5) | ((c) << 2) | ((c) >> 1))
#define CRAM_R(c)          COLOR_3B_TO_8B(BITS((c), 1, 3))
#define CRAM_G(c)          COLOR_3B_TO_8B(BITS((c), 5, 3))
#define CRAM_B(c)          COLOR_3B_TO_8B(BITS((c), 9, 3))
/* Set a pixel on the screen using the Color RAM */
#if PICO_ON_DEVICE
#define set_pixel(scr, x, y, index) scr[x+y*screen_width] = index % 64;
#else
#define __always_inline __inline__
#define __time_critical_func(f) f
#define __fast_mul(a, b) (a*b)
#define set_pixel(scr, x, y, index) scr[(320-screen_width)/2+x+y*320] = (CRAM[index] & 0xe00) >> 7 | (CRAM[index] & 0x0e0) << 3 | (CRAM[index] & 0x00e) << 12;
#endif
#pragma GCC optimize("Ofast")
/*
 * Draw a single pixel of a cell 
 */
static inline __always_inline
void draw_cell_pixel(unsigned int cell, int cell_x, int cell_y, int x, int y) {
    unsigned char* pattern = &VRAM[__fast_mul(cell & 0x7ff, 0x20)];

    int pattern_index = 0;
    if (cell & 0x1000) /* v flip */
        pattern_index = (7 - (cell_y & 7)) << 2;
    else
        pattern_index = (cell_y & 7) << 2;

    if (cell & 0x800) // h flip
        pattern_index += (7 - (cell_x & 7)) >> 1;
    else
        pattern_index += (cell_x & 7) >> 1;

    unsigned char color_index = pattern[pattern_index];

    if ((cell_x & 1) ^ ((cell >> 11) & 1))
        color_index &= 0xf;
    else
        color_index >>= 4;

    if (color_index) {
        color_index += (cell & 0x6000) >> 9;
        set_pixel(screen, x, y, color_index);
    }
}

/*
 * Render the scroll layers (plane A and B)
 */
static inline __always_inline
void vdp_render_bg(int line, int priority) {
static inline void vdp_render_bg(int line, int priority) {
    int h_cells = 32, v_cells = 32;

    switch (vdp_reg[16] & 3) {
        case 0: h_cells = 32;
            break;
        case 1: h_cells = 64;
            break;
        case 3: h_cells = 128;
            break;
    }

    switch ((vdp_reg[16] >> 4) & 3) {
        case 0: v_cells = 32;
            break;
        case 1: v_cells = 64;
            break;
        case 3: v_cells = 128;
            break;
    }

    int hscroll_type = vdp_reg[11] & 3;
    unsigned char* hscroll_table = &VRAM[vdp_reg[13] << 10];
    unsigned int hscroll_mask;
    switch (hscroll_type) {
        case 0x00: hscroll_mask = 0x0000;
            break;
        case 0x01: hscroll_mask = 0x0007;
            break;
        case 0x02: hscroll_mask = 0xfff8;
            break;
        case 0x03: hscroll_mask = 0xffff;
            break;
    }

    const unsigned short vscroll_mask = vdp_reg[11] & 4 ? 0xfff0 : 0x0000;

    for (int scroll_i = 0; scroll_i < 2; scroll_i++) {
        unsigned char* scroll;
        if (scroll_i == 0)
            scroll = &VRAM[vdp_reg[4] << 13];
        else
            scroll = &VRAM[vdp_reg[2] << 10];

        short hscroll = (hscroll_table[__fast_mul(line & hscroll_mask, 4) + __fast_mul(scroll_i ^ 1, 2)] << 8)
                        | hscroll_table[__fast_mul(line & hscroll_mask, 4) + __fast_mul(scroll_i ^ 1, 2) + 1];

        for (int column = 0; column < screen_width; column++) {
            short vscroll = VSRAM[(column & vscroll_mask) / 4 + (scroll_i ^ 1)] & 0x3ff;
            int e_line = (line + vscroll) & (__fast_mul(v_cells, 8) - 1);
            int cell_line = e_line >> 3;
            int e_column = (column - hscroll) & (__fast_mul(h_cells, 8) - 1);
            int cell_column = e_column >> 3;
            unsigned int cell = (scroll[__fast_mul(cell_line * h_cells + cell_column, 2)] << 8)
                                | scroll[__fast_mul(cell_line * h_cells + cell_column, 2) + 1];

            if (((cell & 0x8000) && priority) || ((cell & 0x8000) == 0 && priority == 0))
                draw_cell_pixel(cell, e_column, e_line, column, line);
        }
    }
}

/*
 * Render part of a sprite on a given line.
 */
static inline __always_inline
void vdp_render_sprite(int sprite_index, int line) {
    unsigned char* sprite = &VRAM[(vdp_reg[5] << 9) + __fast_mul(sprite_index, 8)];

    unsigned short y_pos = ((sprite[0] << 8) | sprite[1]) & 0x3ff;
    int h_size = ((sprite[2] >> 2) & 0x3) + 1;
    int v_size = (sprite[2] & 0x3) + 1;
    unsigned int cell = (sprite[4] << 8) | sprite[5];
    unsigned short x_pos = ((sprite[6] << 8) | sprite[7]) & 0x3ff;

    int y = (128 - y_pos + line) & 7;
    int cell_y = (128 - y_pos + line) >> 3;

    for (int cell_x = 0; cell_x < h_size; cell_x++) {

        for (int x = 0; x < 8; x++) {
            int e_x, e_cell;
            e_x = __fast_mul(cell_x, 8) + x + x_pos - 128;
            e_cell = cell;

            if (cell & 0x1000)
                e_cell += v_size - cell_y - 1;
            else
                e_cell += cell_y;

            if (cell & 0x800)
                e_cell += (h_size - cell_x - 1) * v_size;
            else
                e_cell += cell_x * v_size;
            if (e_x >= 0 && e_x < screen_width) {
                draw_cell_pixel(e_cell, x, y, e_x, line);
            }
        }
    }
}

/*
 * Render the sprite layer.
 */
static inline __always_inline
void vdp_render_sprites(int line, int priority) {
    unsigned char* sprite_table = &VRAM[vdp_reg[5] << 9];

    int sprite_queue[80];
    int i = 0;
    int cur_sprite = 0;

    while (1) {
        unsigned char* sprite = &VRAM[(vdp_reg[5] << 9) + __fast_mul(cur_sprite, 8)];
        unsigned short y_pos = (sprite[0] << 8) | sprite[1];
        int v_size = (sprite[2] & 0x3) + 1;
        unsigned int cell = (sprite[4] << 8) | sprite[5];

        int y_min = y_pos - 128;
        int y_max = __fast_mul(v_size - 1, 8) + 7 + y_min;

        if (line >= y_min && line <= y_max) {
            if ((cell >> 15) == priority)
                sprite_queue[i++] = cur_sprite;
        }

        cur_sprite = sprite_table[__fast_mul(cur_sprite, 8) + 3];
        if (!cur_sprite)
            break;

        if (i >= 80)
            break;
    }
    while (i > 0) {
        vdp_render_sprite(sprite_queue[--i], line);
    }
}

/*
 * Render a single line.
 */
// static inline __attribute__((always_inline))
void __time_critical_func(vdp_render_line)(int line) {
    /* Fill the screen with the backdrop color set in register 7 */
#if PICO_ON_DEVICE
    memset(&screen[line*screen_width], vdp_reg[7]&0x3f, 320 );
#else
    for (int i = 0; i < 320; i++) {
        set_pixel(screen, i, line, vdp_reg[7]&0x3f);
    }
#endif

    vdp_render_bg(line, 0);
    vdp_render_sprites(line, 0);
    vdp_render_bg(line, 1);
    vdp_render_sprites(line, 1);
}

void vdp_set_buffers(void* screen_buffer) {
    m68k_pulse_reset();
    screen = screen_buffer;
}

void vdp_debug_status(char* s) {
    s[0] = 0;
    s += sprintf(s, "VDP: ");
    s += sprintf(s, "%04x ", vdp_status);
    for (int i = 0; i < 0x20; i++) {
        if (!(i % 16)) s += sprintf(s, "\n");
        s += sprintf(s, "%02x ", vdp_reg[i]);
    }
}

static inline __attribute__((always_inline))
void vdp_data_write(unsigned int value, int type, int dma) {
    switch (type) {
        case 0: case 1: /* VRAM write */
        VRAM[control_address] = (value >> 8) & 0xff;
        VRAM[control_address + 1] = (value) & 0xff;
        break;
        case 2: case 3: /* CRAM write */
        CRAM[(control_address & 0x7f) >> 1] = value;
#if PICO_ON_DEVICE
        graphics_set_palette((control_address & 0x7f) >> 1, RGB888(CRAM_R(value), CRAM_G(value), CRAM_B(value)));
#endif
        break;
        case 4: case 5: /* VSRAM write */
        VSRAM[(control_address & 0x7f) >> 1] = value;
        break;
    }
}

static inline __attribute__((always_inline))
void vdp_data_port_write(unsigned int value) {
    if (control_code & 1) /* check if write is set */
    {
        vdp_data_write(value, control_code & 0xe, 0);
    }

    control_address = (control_address + vdp_reg[15]) & 0xffff;
    control_pending = 0;

    /* if a DMA is scheduled, do it */
    if (dma_fill) {
        dma_fill = 0;
        dma_length = vdp_reg[19] | (vdp_reg[20] << 8);
        while (dma_length--) {
            VRAM[control_address] = value >> 8;
            control_address = (control_address + vdp_reg[15]) & 0xffff;
        }
    }
}

static inline __attribute__((always_inline))
void vdp_set_reg(int reg, unsigned char value) {
    if (vdp_reg[1] & 4 || reg <= 10)
        vdp_reg[reg] = value;

    control_code = 0;
}

static inline __attribute__((always_inline))
unsigned int vdp_get_reg(int reg) {
    return vdp_reg[reg];
}

static inline __attribute__((always_inline))
void vdp_control_write(unsigned int value) {
    if (!control_pending) {
        if ((value & 0xc000) == 0x8000) {
            int reg = (value >> 8) & 0x1f;
            unsigned char reg_value = value & 0xff;

            vdp_set_reg(reg, reg_value);
        }
        else {
            control_code = (control_code & 0x3c) | ((value >> 14) & 3);
            control_address = (control_address & 0xc000) | (value & 0x3fff);
            control_pending = 1;
        }
    }
    else {
        control_code = (control_code & 3) | ((value >> 2) & 0x3c);
        control_address = (control_address & 0x3fff) | ((value & 3) << 14);
        control_pending = 0;

        if ((control_code & 0x20) && (vdp_reg[1] & 0x10)) {
            if ((vdp_reg[23] >> 6) == 2 && (control_code & 7) == 1) {
                /* DMA fill */
                dma_fill = 1;
            }
            else if ((vdp_reg[23] >> 6) == 3) {
                /* DMA copy */
                // printf("DMA copy\n");
            }
            else {
                /* DMA 68k -> VDP */
                dma_length = vdp_reg[19] | (vdp_reg[20] << 8);
                dma_source = (vdp_reg[21] << 1) | (vdp_reg[22] << 9) | (vdp_reg[23] << 17);

                unsigned int word;

                while (dma_length--) {
                    word = m68k_read_memory_16(dma_source);
                    dma_source += 2;
                    vdp_data_write(word, control_code & 0x7, 1);
                    control_address += vdp_reg[15];
                    control_address &= 0xffff;
                }
            }
        }
    }
}

void vdp_write(unsigned int address, unsigned int value) {
    address &= 0x1f;

    if (address < 0x04) {
        vdp_data_port_write(value);
    }
    else if (address >= 0x04 && address < 0x08) {
        vdp_control_write(value);
    }
    else {
        // printf("vdp_write(%x, %x)\n", address, value);
    }
}

unsigned int vdp_read(unsigned int address) {
    address &= 0x1f;

    if (0 && address < 0x04) {
    }
    else if (address >= 0x04 && address < 0x08) {
        /* VDP status */
        return vdp_status;
    }
    else if (address >= 0x08 && address < 0x10) {
        /* V/H counter */
        extern int cycle_counter;
        extern int lines_per_frame;
        extern int MCYCLES_PER_LINE;

        int vcounter, hcounter;

        vcounter = cycle_counter / MCYCLES_PER_LINE - 1;
        if (vcounter > (vdp_reg[1] & 0x08 ? 262 : 234)) {
            vcounter -= lines_per_frame;
        }

        if (vdp_reg[12] & 0x01) {
            hcounter = 0;
        }
        else {
            hcounter = ((cycle_counter + 10) % MCYCLES_PER_LINE) / 20;
            if (hcounter >= 12)
                hcounter += 0x56;
            hcounter += 0x85;
        }

        if (address & 1)
            return hcounter & 0xff;
        else
            return vcounter & 0xff;
    }
    else {
//        printf("vdp_read(%x)\n", address);
    }
    return 0;
}

static inline __attribute__((always_inline))
unsigned int vdp_get_status() {
    return vdp_status;
}

static inline __attribute__((always_inline))
uint16_t vdp_get_cram(int index) {
    return CRAM[index & 0x3f];
}

void vdp_set_hblank() {
    vdp_status |= 4;
}

void vdp_clear_hblank() {
    vdp_status &= ~4;
}

void vdp_set_vblank() {
    vdp_status |= 8;
}

void vdp_clear_vblank() {
    vdp_status &= ~8;
}
