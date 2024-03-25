#pragma once
/*
 * Megadrive VDP emulation
 */

// static inline void draw_cell_pixel(unsigned int cell, int cell_x, int cell_y, int x, int y);
// void vdp_render_bg(int line, int priority);
// void vdp_render_sprite(int sprite_index, int line);
// void vdp_render_sprites(int line, int priority);
void vdp_render_line(int line);

void vdp_set_buffers(void *screen_buffer);
void vdp_debug_status(char *s);

enum ram_type {
    T_VRAM,
    T_CRAM,
    T_VSRAM
};

// static inline void vdp_data_write(unsigned int value, int type, int dma);
// static inline void vdp_data_port_write(unsigned int value);
// void vdp_set_reg(int reg, unsigned char value);
// unsigned int vdp_get_reg(int reg);
// void vdp_control_write(unsigned int value);
void vdp_write(unsigned int address, unsigned int value);
unsigned int vdp_read(unsigned int address);

// unsigned int vdp_get_status();
// static inline unsigned short vdp_get_cram(int index);

void vdp_set_hblank();
void vdp_clear_hblank();
void vdp_set_vblank();
void vdp_clear_vblank();
