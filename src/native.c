#pragma once
#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef USE_SDL
SDL_Window *window1;
SDL_Surface *screen;
const Uint8 *keystates;
unsigned int *bitmap;
#else
//extern uint16_t bitmap[288*256];
#endif
#ifdef EMCCHACK
#define RGB555toRGB24(t) \
            ( (((t)&0x1f)<<3) | (((t)&0x3e0)<<6) | (((t)&0x7c00)<<9) )
#else
#endif


void native_hardware_init(int spcemu) {
#ifdef USE_SDL

    //if( SDL_Init( SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_AUDIO) == -1 )  exit(-1);
    // \    SDL_Init(SDL_INIT_VIDEO);
    SDL_Init(SDL_INIT_VIDEO);

    window1 = SDL_CreateWindow("SDL2 Demo",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               288, 256,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    screen = SDL_GetWindowSurface(window1);
    bitmap = (uint32_t *) screen->pixels;
    printf("Pixel format: %s\n",
           SDL_GetPixelFormatName(screen->format->format));

    //keystates = SDL_GetKeyState( NULL );
#ifdef EMCCHACK
    keystates = SDL_GetKeyboardState( NULL );
#else
    atexit(SDL_Quit);
    keystates = 0;
#endif
#endif
#ifdef SOUND
    if (spcemu) initsound();
        if (spcemu) install_int_ex(soundcheck,BPS_TO_TIMER(500));
#endif
}


uint32_t *native_bitmap_pointer(int x, int y) {
    return (void *) (&bitmap[(y + 16) * 288 + x + 16]);
}

static int cnt = 0;

void native_bitmap_to_screen() {
#ifdef USE_SDL
/*  uint32_t *bp=(uint32_t *)native_bitmap_pointer(-16,-16);
  int x,y;
  for(y=0;y<16*288;y++)
    *bp++=0;
  bp=(uint32_t *)native_bitmap_pointer(-16,224);
  for(y=0;y<16*288;y++)
    *bp++=0;
  for(y=0;y<=224;y++){
    bp=(uint32_t *)native_bitmap_pointer(-32,y);
    for(x=0;x<32;x++)
      *bp++=0;
  }*/
/*    for(int y=0; y < 240; y++)
    for(int x=0; x < 320; x++) {
    bitmap[x + y * 320] = SDL_MapRGBA(screen->format, 255, 255, 255, 255);
            }
*/
    //SDL_UnlockSurface(screen);
    SDL_UpdateWindowSurface(window1);
    //SDL_LockSurface(screen);
#endif
#ifdef ASCII
    if((cnt&7)==0){
    drawansi(256, 224, (void *)native_bitmap_pointer(0,0), 32, 288*4);
    printf("%d\n",cnt);
    }
#endif

    cnt++;

}

void native_bitmap_clear_line(int line, uint32_t color) {
    int x;
    uint32_t *bp = (uint32_t *) native_bitmap_pointer(0, line);
    for (x = 0; x < 256; x++)
        bp[x] = color;
    //memset(native_bitmap_pointer(0, line), 0, 264*2);
}

int native_poll_keyboard() {
#if defined(USE_SDL)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) { return -1; }
    }
    if (keystates[SDLK_ESCAPE]) { return -1; }
#else

#endif
    return 0;

}

#ifdef USE_SDL

static uint32_t my_callbackfunc(uint32_t interval, void *param) {

    int (*func)(void);
    int stop_running;
    func = (int (*)(void)) param;
    stop_running = func();
    if (stop_running)
        return 0;
#ifdef EMCCHACK
    SDL_AddTimer( interval, my_callbackfunc, param);
#endif
    return (interval);
}

#endif

void native_tick_callback(int (*func)(void), int fps) {
#ifdef USE_SDL
    SDL_AddTimer(1000 / fps, my_callbackfunc, (void *) func);
#endif
}

static uint32_t joy1state = 0x80000000;

uint32_t *native_set_joypad_state(uint32_t state) {
    joy1state = state;
    return &joy1state;
}

uint32_t native_joypad_state(int num) {
    uint32_t joy1 = 0x80000000;
    if (num != 0) return joy1;
#ifdef USE_SDL
    //native_poll_keyboard();
    const Uint8 *currentKeyStates = SDL_GetKeyboardState(NULL);
    if (currentKeyStates[SDL_SCANCODE_W]) joy1 |= 0x0010;
    if (currentKeyStates[SDL_SCANCODE_Q]) joy1 |= 0x0020;
    if (currentKeyStates[SDL_SCANCODE_A]) joy1 |= 0x0040;
    if (currentKeyStates[SDL_SCANCODE_S]) joy1 |= 0x0080;
    if (currentKeyStates[SDL_SCANCODE_RIGHT]) joy1 |= 0x0100;
    if (currentKeyStates[SDL_SCANCODE_LEFT]) joy1 |= 0x0200;
    if (currentKeyStates[SDL_SCANCODE_DOWN]) joy1 |= 0x0400;
    if (currentKeyStates[SDL_SCANCODE_UP]) joy1 |= 0x0800;
    if (currentKeyStates[SDL_SCANCODE_C]) joy1 |= 0x1000;
    if (currentKeyStates[SDL_SCANCODE_D]) joy1 |= 0x2000;
    if (currentKeyStates[SDL_SCANCODE_Z]) joy1 |= 0x4000;
    if (currentKeyStates[SDL_SCANCODE_X]) joy1 |= 0x8000;
#else
    /*if(cnt%120==111)
        joy1|=(cnt&0xffff);*/
    joy1=joy1state;

#endif
    return joy1;
}