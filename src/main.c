#pragma GCC optimize("Ofast")
/* See LICENSE file for license details */

/* Standard library includes */
#include "neopop/neopop.h"

#include <stdint.h>
#include <stdbool.h>

size_t filesize = 0;
#define  AUDIO_FREQ DAC_FREQUENCY
#define AUDIO_BUFFER_LENGTH (AUDIO_FREQ /60 +1)
//int16_t AudioBuffer[HANDY_AUDIO_BUFFER_LENGTH] = { 0 };
#if !PICO_ON_DEVICE
#include <windows.h>
#include "MiniFB.h"


uint8_t ROM[0x400000];
extern uint16_t cfb[256*256];

#else
#include <hardware/clocks.h>
#include <hardware/flash.h>
#include <hardware/vreg.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <graphics.h>

#include "nespad.h"
#include "ps2kbd_mrmltr.h"
#include "ff.h"

#define HOME_DIR "\\PCE"
extern char __flash_binary_end;
// #define FLASH_TARGET_OFFSET (((((uintptr_t)&__flash_binary_end - XIP_BASE) / FLASH_SECTOR_SIZE) + 1) * FLASH_SECTOR_SIZE)
// uintptr_t ROM = XIP_BASE + FLASH_TARGET_OFFSET;
#define FLASH_TARGET_OFFSET (1024 * 1024)
const uint8_t* ROM = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

semaphore vga_start_semaphore;
uint8_t SCREEN[XBUF_HEIGHT][XBUF_WIDTH];
static FATFS fs;
#endif

bool reboot = false;

#if !PICO_ON_DEVICE
void readfile(const char* pathname, uint8_t* dst) {

    FILE* file = fopen(pathname, "rb");
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    filesize = size;
    fread(dst, sizeof(uint8_t), size, file);
}
#else
typedef struct __attribute__((__packed__)) {
    bool a: 1;
    bool b: 1;
    bool c: 1;
    bool x: 1;
    bool y: 1;
    bool z: 1;
    bool mode: 1;
    bool start: 1;
    bool right: 1;
    bool left: 1;
    bool up: 1;
    bool down: 1;
} input_bits_t;

static input_bits_t keyboard_bits = {};
static input_bits_t gamepad1_bits = {};
static input_bits_t gamepad2_bits = {};

void nespad_tick() {
    int smsButtons = 0;
    int smsSystem = 0 ;
    nespad_read();

    gamepad1_bits.a = (nespad_state & DPAD_A) != 0;
    gamepad1_bits.b = (nespad_state & DPAD_B) != 0;
    gamepad1_bits.c = (nespad_state & DPAD_SELECT) != 0;
    gamepad1_bits.start = (nespad_state & DPAD_START) != 0;
    gamepad1_bits.up = (nespad_state & DPAD_UP) != 0;
    gamepad1_bits.down = (nespad_state & DPAD_DOWN) != 0;
    gamepad1_bits.left = (nespad_state & DPAD_LEFT) != 0;
    gamepad1_bits.right = (nespad_state & DPAD_RIGHT) != 0;

    gamepad2_bits.a = (nespad_state2 & DPAD_A) != 0;
    gamepad2_bits.b = (nespad_state2 & DPAD_B) != 0;
    gamepad2_bits.c = (nespad_state2 & DPAD_SELECT) != 0;
    gamepad2_bits.start = (nespad_state2 & DPAD_START) != 0;
    gamepad2_bits.up = (nespad_state2 & DPAD_UP) != 0;
    gamepad2_bits.down = (nespad_state2 & DPAD_DOWN) != 0;
    gamepad2_bits.left = (nespad_state2 & DPAD_LEFT) != 0;
    gamepad2_bits.right = (nespad_state2 & DPAD_RIGHT) != 0;

}

static bool isInReport(hid_keyboard_report_t const* report, const unsigned char keycode) {
    for (unsigned char i: report->keycode) {
        if (i == keycode) {
            return true;
        }
    }
    return false;
}

void
__not_in_flash_func(process_kbd_report)(hid_keyboard_report_t const* report, hid_keyboard_report_t const* prev_report) {
    /* printf("HID key report modifiers %2.2X report ", report->modifier);
    for (unsigned char i: report->keycode)
        printf("%2.2X", i);
    printf("\r\n");
     */
    keyboard_bits.c = isInReport(report, HID_KEY_ESCAPE);
    keyboard_bits.mode = isInReport(report, HID_KEY_BACKSPACE);
    keyboard_bits.a = isInReport(report, HID_KEY_A);
    keyboard_bits.b = isInReport(report, HID_KEY_S);
    keyboard_bits.start = isInReport(report, HID_KEY_ENTER);
    keyboard_bits.x = isInReport(report, HID_KEY_Z);
    keyboard_bits.y = isInReport(report, HID_KEY_X);
    keyboard_bits.z = isInReport(report, HID_KEY_C);
    keyboard_bits.up = isInReport(report, HID_KEY_ARROW_UP);
    keyboard_bits.down = isInReport(report, HID_KEY_ARROW_DOWN);
    keyboard_bits.left = isInReport(report, HID_KEY_ARROW_LEFT);
    keyboard_bits.right = isInReport(report, HID_KEY_ARROW_RIGHT);
    //-------------------------------------------------------------------------
}

Ps2Kbd_Mrmltr ps2kbd(
    pio1,
    0,
    process_kbd_report);

/* Renderer loop on Pico's second core */
void __scratch_x("render") render_core() {
    multicore_lockout_victim_init();

    ps2kbd.init_gpio();
    nespad_begin(clock_get_hz(clk_sys) / 1000, NES_GPIO_CLK, NES_GPIO_DATA, NES_GPIO_LAT);

    graphics_init();

    const auto buffer = (uint8_t *)SCREEN;
    graphics_set_buffer(buffer, XBUF_WIDTH, XBUF_HEIGHT);
    // graphics_set_offset(32, 24);
    graphics_set_offset(0, 0);
    graphics_set_textbuffer(buffer);
    graphics_set_bgcolor(0x000000);

    graphics_set_flashmode(true, true);
    sem_acquire_blocking(&vga_start_semaphore);

    // 60 FPS loop
#define frame_tick (16666)
    uint64_t tick = time_us_64();
    uint64_t last_frame_tick = tick;
    int old_frame = 0;

    while (true) {
        if (tick >= last_frame_tick + frame_tick) {
#ifdef TFT
            refresh_lcd();
#endif
            ps2kbd.tick();
            nespad_tick();

            last_frame_tick = tick;
        }

        tick = time_us_64();

        // tuh_task();
        // hid_app_task();

        tight_loop_contents();
    }

    __unreachable();
}

uint16_t frequencies[] = {378, 396, 404, 408, 412, 416, 420, 424, 432};
uint8_t frequency_index = 0;

bool overclock() {
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    sleep_ms(10);
    return set_sys_clock_khz(frequencies[frequency_index] * 1000, true);
}

typedef struct __attribute__((__packed__)) {
    bool is_directory;
    bool is_executable;
    size_t size;
    char filename[79];
} file_item_t;

constexpr int max_files = 600;
file_item_t* fileItems = (file_item_t *)(&SCREEN[0][0] + TEXTMODE_COLS * TEXTMODE_ROWS * 2);

int compareFileItems(const void* a, const void* b) {
    const auto* itemA = (file_item_t *)a;
    const auto* itemB = (file_item_t *)b;
    // Directories come first
    if (itemA->is_directory && !itemB->is_directory)
        return -1;
    if (!itemA->is_directory && itemB->is_directory)
        return 1;
    // Sort files alphabetically
    return strcmp(itemA->filename, itemB->filename);
}

bool isExecutable(const char pathname[255], const char* extensions) {
    char* pathCopy = strdup(pathname);
    const char* token = strrchr(pathCopy, '.');

    if (token == nullptr) {
        return false;
    }

    token++;

    while (token != NULL) {
        if (strstr(extensions, token) != NULL) {
            free(pathCopy);
            return true;
        }
        token = strtok(NULL, ",");
    }
    free(pathCopy);
    return false;
}

bool filebrowser_loadfile(const char pathname[256]) {
    UINT bytes_read = 0;
    FIL file;

    constexpr int window_y = (TEXTMODE_ROWS - 5) / 2;
    constexpr int window_x = (TEXTMODE_COLS - 43) / 2;

    draw_window("Loading firmware", window_x, window_y, 43, 5);

    FILINFO fileinfo;
    f_stat(pathname, &fileinfo);
    filesize = fileinfo.fsize;

    if (16384 - 64 << 10 < fileinfo.fsize) {
        draw_text("ERROR: ROM too large! Canceled!!", window_x + 1, window_y + 2, 13, 1);
        sleep_ms(5000);
        return false;
    }


    draw_text("Loading...", window_x + 1, window_y + 2, 10, 1);
    sleep_ms(500);


    multicore_lockout_start_blocking();
    auto flash_target_offset = FLASH_TARGET_OFFSET;
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_target_offset, fileinfo.fsize);
    restore_interrupts(ints);

    if (FR_OK == f_open(&file, pathname, FA_READ)) {
        uint8_t buffer[FLASH_PAGE_SIZE];

        do {
            f_read(&file, &buffer, FLASH_PAGE_SIZE, &bytes_read);

            if (bytes_read) {
                const uint32_t ints = save_and_disable_interrupts();
                flash_range_program(flash_target_offset, buffer, FLASH_PAGE_SIZE);
                restore_interrupts(ints);

                gpio_put(PICO_DEFAULT_LED_PIN, flash_target_offset >> 13 & 1);

                flash_target_offset += FLASH_PAGE_SIZE;
            }
        }
        while (bytes_read != 0);

        gpio_put(PICO_DEFAULT_LED_PIN, true);
    }
    f_close(&file);
    multicore_lockout_end_blocking();
    // restore_interrupts(ints);
    return true;
}

void filebrowser(const char pathname[256], const char executables[11]) {
    bool debounce = true;
    char basepath[256];
    char tmp[TEXTMODE_COLS + 1];
    strcpy(basepath, pathname);
    constexpr int per_page = TEXTMODE_ROWS - 3;

    DIR dir;
    FILINFO fileInfo;

    if (FR_OK != f_mount(&fs, "SD", 1)) {
        draw_text("SD Card not inserted or SD Card error!", 0, 0, 12, 0);
        while (true);
    }

    while (true) {
        memset(fileItems, 0, sizeof(file_item_t) * max_files);
        int total_files = 0;

        snprintf(tmp, TEXTMODE_COLS, "SD:\\%s", basepath);
        draw_window(tmp, 0, 0, TEXTMODE_COLS, TEXTMODE_ROWS - 1);
        memset(tmp, ' ', TEXTMODE_COLS);


        draw_text(tmp, 0, 29, 0, 0);
        auto off = 0;
        draw_text("START", off, 29, 7, 0);
        off += 5;
        draw_text(" Run at cursor ", off, 29, 0, 3);
        off += 16;
        draw_text("SELECT", off, 29, 7, 0);
        off += 6;
        draw_text(" Run previous  ", off, 29, 0, 3);
#ifndef TFT
        off += 16;
        draw_text("ARROWS", off, 29, 7, 0);
        off += 6;
        draw_text(" Navigation    ", off, 29, 0, 3);
        off += 16;
        draw_text("A/F10", off, 29, 7, 0);
        off += 5;
        draw_text(" USB DRV ", off, 29, 0, 3);
#endif

        if (FR_OK != f_opendir(&dir, basepath)) {
            draw_text("Failed to open directory", 1, 1, 4, 0);
            while (true);
        }

        if (strlen(basepath) > 0) {
            strcpy(fileItems[total_files].filename, "..\0");
            fileItems[total_files].is_directory = true;
            fileItems[total_files].size = 0;
            total_files++;
        }

        while (f_readdir(&dir, &fileInfo) == FR_OK &&
               fileInfo.fname[0] != '\0' &&
               total_files < max_files
        ) {
            // Set the file item properties
            fileItems[total_files].is_directory = fileInfo.fattrib & AM_DIR;
            fileItems[total_files].size = fileInfo.fsize;
            fileItems[total_files].is_executable = isExecutable(fileInfo.fname, executables);
            strncpy(fileItems[total_files].filename, fileInfo.fname, 78);
            total_files++;
        }
        f_closedir(&dir);

        qsort(fileItems, total_files, sizeof(file_item_t), compareFileItems);

        if (total_files > max_files) {
            draw_text(" Too many files!! ", TEXTMODE_COLS - 17, 0, 12, 3);
        }

        int offset = 0;
        int current_item = 0;

        while (true) {
            sleep_ms(100);

            if (!debounce) {
                debounce = !(nespad_state & DPAD_START || keyboard_bits.start);
            }

            // ESCAPE
            if (nespad_state & DPAD_SELECT || keyboard_bits.c) {
                return;
            }

            if (nespad_state & DPAD_DOWN || keyboard_bits.down) {
                if (offset + (current_item + 1) < total_files) {
                    if (current_item + 1 < per_page) {
                        current_item++;
                    }
                    else {
                        offset++;
                    }
                }
            }

            if (nespad_state & DPAD_UP || keyboard_bits.up) {
                if (current_item > 0) {
                    current_item--;
                }
                else if (offset > 0) {
                    offset--;
                }
            }

            if (nespad_state & DPAD_RIGHT || keyboard_bits.right) {
                offset += per_page;
                if (offset + (current_item + 1) > total_files) {
                    offset = total_files - (current_item + 1);
                }
            }

            if (nespad_state & DPAD_LEFT || keyboard_bits.left) {
                if (offset > per_page) {
                    offset -= per_page;
                }
                else {
                    offset = 0;
                    current_item = 0;
                }
            }

            if (debounce && (nespad_state & DPAD_START || keyboard_bits.start)) {
                auto file_at_cursor = fileItems[offset + current_item];

                if (file_at_cursor.is_directory) {
                    if (strcmp(file_at_cursor.filename, "..") == 0) {
                        const char* lastBackslash = strrchr(basepath, '\\');
                        if (lastBackslash != nullptr) {
                            const size_t length = lastBackslash - basepath;
                            basepath[length] = '\0';
                        }
                    }
                    else {
                        sprintf(basepath, "%s\\%s", basepath, file_at_cursor.filename);
                    }
                    debounce = false;
                    break;
                }

                if (file_at_cursor.is_executable) {
                    sprintf(tmp, "%s\\%s", basepath, file_at_cursor.filename);

                    filebrowser_loadfile(tmp);
                    return;
                }
            }

            for (int i = 0; i < per_page; i++) {
                uint8_t color = 11;
                uint8_t bg_color = 1;

                if (offset + i < max_files) {
                    const auto item = fileItems[offset + i];


                    if (i == current_item) {
                        color = 0;
                        bg_color = 3;
                        memset(tmp, 0xCD, TEXTMODE_COLS - 2);
                        tmp[TEXTMODE_COLS - 2] = '\0';
                        draw_text(tmp, 1, per_page + 1, 11, 1);
                        snprintf(tmp, TEXTMODE_COLS - 2, " Size: %iKb, File %lu of %i ", item.size / 1024,
                                 offset + i + 1,
                                 total_files);
                        draw_text(tmp, 2, per_page + 1, 14, 3);
                    }

                    const auto len = strlen(item.filename);
                    color = item.is_directory ? 15 : color;
                    color = item.is_executable ? 10 : color;
                    //color = strstr((char *)rom_filename, item.filename) != nullptr ? 13 : color;

                    memset(tmp, ' ', TEXTMODE_COLS - 2);
                    tmp[TEXTMODE_COLS - 2] = '\0';
                    memcpy(&tmp, item.filename, len < TEXTMODE_COLS - 2 ? len : TEXTMODE_COLS - 2);
                }
                else {
                    memset(tmp, ' ', TEXTMODE_COLS - 2);
                }
                draw_text(tmp, 1, i + 1, color, bg_color);
            }
        }
    }
}
#endif


#define JOYPORT_ADDR	0x6F82

static void update_input()
{
    uint8_t buttons = 0;
#if !PICO_ON_DEVICE

    /*    if (key_status[0x25]) buttons |= BUTTON_LEFT;
    if (key_status[0x27]) buttons |= BUTTON_RIGHT;
    if (key_status[0x26]) buttons |= BUTTON_UP;
    if (key_status[0x28]) buttons |= BUTTON_DOWN;
    if (key_status['Z']) buttons |= BUTTON_A;
    if (key_status['X']) buttons |= BUTTON_B;
    if (key_status[0x0d]) buttons |= BUTTON_OPT2;
    if (key_status[0x20]) buttons |= BUTTON_OPT1;*/
    static const int joy_mask[] = {
            0x01, /* up */
            0x02, /* down */
            0x04, /* left */
            0x08, /* right */
            0x10, /* button a */
            0x20, /* button b */
            0x40  /* option */
    };
    uint8_t * key_status  = (uint8_t *)mfb_keystatus();
    if (key_status[0x25]) buttons |= 0x04;
    if (key_status[0x27]) buttons |= 0x08;
    if (key_status[0x26]) buttons |= 0x01;
    if (key_status[0x28]) buttons |= 0x02;
    if (key_status['Z']) buttons |= 0x10;
    if (key_status['X']) buttons |= 0x20;
    //if (key_status[0x0d]) buttons |= 0x40;
    if (key_status[0x20]) buttons |= 0x40;
#endif
    ram[JOYPORT_ADDR] = buttons;
}


DWORD WINAPI SoundThread(LPVOID lpParam) {
    WAVEHDR waveHeaders[4];

    WAVEFORMATEX format = { 0 };
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = AUDIO_FREQ ;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HANDLE waveEvent = CreateEvent(NULL, 1,0, NULL);

    HWAVEOUT hWaveOut;
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &format, (DWORD_PTR)waveEvent , 0, CALLBACK_EVENT);

    for (size_t i = 0; i < 4; i++) {
        int16_t audio_buffers[4][AUDIO_BUFFER_LENGTH * 2];
        waveHeaders[i] = (WAVEHDR) {
            .lpData = (char*)audio_buffers[i],
            .dwBufferLength = AUDIO_BUFFER_LENGTH * 2,
        };
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        waveHeaders[i].dwFlags |= WHDR_DONE;
    }
    WAVEHDR* currentHeader = waveHeaders;


    while (true) {
        if (WaitForSingleObject(waveEvent, INFINITE)) {
            fprintf(stderr, "Failed to wait for event.\n");
            return 1;
        }

        if (!ResetEvent(waveEvent)) {
            fprintf(stderr, "Failed to reset event.\n");
            return 1;
        }

        // Wait until audio finishes playing
         while (currentHeader->dwFlags & WHDR_DONE) {
//             memcpy(currentHeader->lpData, AudioBuffer, AUDIO_BUFFER_LENGTH * 2);
             //             auto * ptr = (unsigned short *)currentHeader->lpData;

//             sound_stream_update(buffer, AUDIO_BUFFER_SIZE);
             //psg_update((int16_t *)currentHeader->lpData, AUDIO_BUFFER_LENGTH , 0xff);
             /* Convert from U8 (0 - 45) to S16 */
//             for (unsigned char i : AudioBuffer)
//                 *ptr++ = i  << (8 + 1);


             waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));

             currentHeader++;
             if (currentHeader == waveHeaders + 4) { currentHeader = waveHeaders; }
         }
    }
    return 0;
}
void system_message(char *vaMessage, ...) {
    va_list vl;

    va_start(vl, vaMessage);
    vprintf(vaMessage, vl);
    va_end(vl);
    printf("\n");
}

void system_sound_chipreset(void) {}
void system_sound_silence(void) {}
BOOL system_comms_read(_u8* buffer) { return false; }
BOOL system_comms_poll(_u8* buffer) { return false; }
void system_comms_write(_u8 data) {}

///
BOOL system_io_flash_read(_u8* buffer, _u32 bufferLength) {
    return false;
}
///
BOOL system_io_flash_write(_u8* buffer, _u32 bufferLength) {
    return false;
}

void system_VBL(void) {

    // frame drawn
    update_input();

    if (mfb_update(cfb, 60) == -1)
        exit(0);
}

BOOL system_io_state_read(char* filename, _u8* buffer, _u32 bufferLength) {
    return false;
}
BOOL system_io_state_write(char* filename, _u8* buffer, _u32 bufferLength) {
    return false;
}
uint8_t system_frameskip_key;

/* copied from Win32/system_language.c */
typedef struct {
    char label[9];
    char string[256];
} STRING_TAG;

static STRING_TAG string_tags[]={
        { "SDEFAULT",     "Are you sure you want to revert to the default control setup?" },
        { "ROMFILT",      "Rom Files (*.ngp,*.ngc,*.npc,*.zip)\0*.ngp;*.ngc;*.npc;*.zip\0\0" },
        { "STAFILT",      "State Files (*.ngs)\0*.ngs\0\0" },
        { "FLAFILT",      "Flash Memory Files (*.ngf)\0*.ngf\0\0" },
        { "BADFLASH",     "The flash data for this rom is from a different version of NeoPop, it will be destroyed soon." },
        { "POWER",        "The system has been signalled to power down. You must reset or load a new rom." },
        { "BADSTATE",     "State is from an unsupported version of NeoPop." },
        { "ERROR1",       "An error has occured creating the application window" },
        { "ERROR2",       "An error has occured initialising DirectDraw" },
        { "ERROR3",       "An error has occured initialising DirectInput" },
        { "TIMER",        "This system does not have a high resolution timer." },
        { "WRONGROM",     "This state is from a different rom, Ignoring." },
        { "EROMFIND",     "Cannot find ROM file" },
        { "EROMOPEN",     "Cannot open ROM file" },
        { "EZIPNONE",     "No roms found" } ,
        { "EZIPBAD",      "Corrupted ZIP file" },
        { "EZIPFIND",     "Cannot find ZIP file" },

        { "ABORT",	      "Abort" },
        { "DISCON",	      "Disconnect" },
        { "CONNEC",	      "Connected" }
};

char *
system_get_string(STRINGS string_id)
{
    if (string_id >= STRINGS_MAX)
        return "Unknown String";

    return string_tags[string_id].string;
}

int main(int argc, char** argv) {
#if !PICO_ON_DEVICE
    readfile(argv[1], ROM);

#else
    overclock();

    stdio_init_all();

    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(render_core);
    sem_release(&vga_start_semaphore);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    for (int i = 0; i < 6; i++) {
        sleep_ms(33);
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        sleep_ms(33);
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    }
    graphics_set_mode(TEXTMODE_DEFAULT);
    filebrowser(HOME_DIR, "pce");
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
#endif

    system_colour = COLOURMODE_AUTO;
    language_english = true;
    mute = false;

    bios_install();
    rom.data = ROM;
    rom.length = filesize;
    rom_loaded();
    system_frameskip_key = 1;
//    sound_init(44100);
    reset();

        if (!mfb_open("neopop", SCREEN_WIDTH, SCREEN_HEIGHT, 5))
            return 0;
    // Create sound thread
    //HANDLE hThread = CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);
//    lynx->mMikie->SetRotation(MIKIE_NO_ROTATE);
    for (;;) {
        emulate();
    }
}
