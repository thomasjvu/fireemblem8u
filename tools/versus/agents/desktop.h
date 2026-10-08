/* Native SDL2 frontend for the same linked mGBA cores used by agents. */
#include <SDL.h>
#include "sdl-audio.h"
#include <sys/select.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_mutex *pixelMutex;
static struct mSDLAudio nativeAudio;
static bool audioStarted, desktopQuit;
static mColor displayPixels[2][240 * 160];
static atomic_uint keyboardKeys, armedSeats, injectedKeys[2];
static unsigned humanSeats;
static SDL_GameController *controllers[2];
static Uint64 frameClock[2], lastPresent;

static unsigned keyboardButton(SDL_Keycode key) {
    switch (key) {
    case SDLK_x: return 1;       /* A */
    case SDLK_z: return 2;       /* B */
    case SDLK_BACKSPACE: return 4; /* Select */
    case SDLK_RETURN: return 8;  /* Start */
    case SDLK_RIGHT: return 16;
    case SDLK_LEFT: return 32;
    case SDLK_UP: return 64;
    case SDLK_DOWN: return 128;
    case SDLK_w: return 256;     /* R */
    case SDLK_q: return 512;     /* L */
    default: return 0;
    }
}
static unsigned controllerButtons(SDL_GameController *pad) {
    if (!pad || !SDL_GameControllerGetAttached(pad)) return 0;
    unsigned keys = 0;
    const SDL_GameControllerButton buttons[] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER
    };
    for (unsigned i = 0; i < 10; i++)
        if (SDL_GameControllerGetButton(pad, buttons[i])) keys |= 1u << i;
    int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (x > 16000) keys |= 16;
    if (x < -16000) keys |= 32;
    if (y < -16000) keys |= 64;
    if (y > 16000) keys |= 128;
    return keys;
}
static void desktopPads(void) {
    for (int i = 0; i < 2; i++) {
        if (controllers[i] && !SDL_GameControllerGetAttached(controllers[i])) {
            SDL_GameControllerClose(controllers[i]); controllers[i] = NULL;
        }
    }
    for (int j = 0; j < SDL_NumJoysticks(); j++) {
        if (!SDL_IsGameController(j)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
        bool found = false;
        for (int i = 0; i < 2; i++)
            if (controllers[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == id)
                found = true;
        if (!found) for (int i = 0; i < 2; i++) if (!controllers[i]) {
            controllers[i] = SDL_GameControllerOpen(j); break;
        }
    }
}
static bool desktopInit(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow("Fire Emblem Versus — Native mGBA", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1200, 800, SDL_WINDOW_RESIZABLE);
    if (!window) return false;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) return false;
    SDL_RenderSetLogicalSize(renderer, 240, 160);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STREAMING, 240, 160);
    pixelMutex = SDL_CreateMutex();
    humanSeats = strtoul(getenv("VERSUS_HUMAN_SEATS") ? getenv("VERSUS_HUMAN_SEATS") : "0", NULL, 10) & 3;
    desktopPads();
    return texture && pixelMutex;
}
static void desktopFrame(struct Player *p) {
    unsigned seat = p == &players[1];
    SDL_LockMutex(pixelMutex);
    memcpy(displayPixels[seat], p->pixels, sizeof(p->pixels));
    SDL_UnlockMutex(pixelMutex);
    /* Both cores advance at native GBA rate, independently of browser/agent timing. */
    Uint64 now = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();
    Uint64 step = (Uint64)(frequency / 59.727500569606);
    if (!frameClock[seat] || now > frameClock[seat] + step * 5) frameClock[seat] = now;
    frameClock[seat] += step;
    while ((now = SDL_GetPerformanceCounter()) < frameClock[seat]) {
        Uint64 remaining = (frameClock[seat] - now) * 1000000 / frequency;
        if (remaining > 1000) SDL_Delay((Uint32)(remaining / 1000));
        else break;
    }
}
static unsigned desktopInput(struct Player *p, struct mCore *c) {
    unsigned seat = p == &players[1];
    if (!(humanSeats & atomic_load(&armedSeats) & (1u << seat)) ||
        c->busRead8(c, 0x0203f355) != seat || c->busRead8(c, 0x0203f358)) return 0;
    return atomic_load(&p->inputKeys);
}
static void desktopPump(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) desktopQuit = true;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            atomic_store(&keyboardKeys, 0);
        if (event.type == SDL_KEYDOWN && !event.key.repeat)
            atomic_fetch_or(&keyboardKeys, keyboardButton(event.key.keysym.sym));
        if (event.type == SDL_KEYUP)
            atomic_fetch_and(&keyboardKeys, ~keyboardButton(event.key.keysym.sym));
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED)
            desktopPads();
    }
    unsigned keyboard = atomic_load(&keyboardKeys);
    bool twoPads = controllers[0] && controllers[1];
    for (unsigned seat = 0; seat < 2; seat++) {
        SDL_GameController *pad = twoPads ? controllers[seat] : (controllers[0] ? controllers[0] : controllers[1]);
        atomic_store(&players[seat].inputKeys, keyboard | controllerButtons(pad) | atomic_load(&injectedKeys[seat]));
    }
    Uint64 now = SDL_GetPerformanceCounter();
    if (now - lastPresent < SDL_GetPerformanceFrequency() / 60) return;
    lastPresent = now;
    /* Follow the active army's native view; the ROM keeps both battle states synchronized. */
    unsigned seat = atomic_load(&players[0].active) & 1;
    SDL_LockMutex(pixelMutex);
    SDL_UpdateTexture(texture, NULL, displayPixels[seat], 240 * sizeof(mColor));
    SDL_UnlockMutex(pixelMutex);
    SDL_SetRenderDrawColor(renderer, 8, 13, 23, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
static bool desktopLine(char *line, size_t size) {
    while (!desktopQuit) {
        desktopPump();
        fd_set ready; FD_ZERO(&ready); FD_SET(STDIN_FILENO, &ready);
        struct timeval timeout = {0, 8000};
        int available = select(STDIN_FILENO + 1, &ready, NULL, NULL, &timeout);
        if (available > 0) return fgets(line, (int)size, stdin) != NULL;
        if (available < 0) return false;
    }
    return false;
}
static void desktopClose(void) {
    if (audioStarted) mSDLDeinitAudio(&nativeAudio);
    for (int i = 0; i < 2; i++) if (controllers[i]) SDL_GameControllerClose(controllers[i]);
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    SDL_DestroyMutex(pixelMutex); SDL_Quit();
}
