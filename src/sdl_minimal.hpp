#pragma once

#include <cstdint>

extern "C" {

struct SDL_Window;
using SDL_GLContext = void*;
using Uint8 = std::uint8_t;
using Uint16 = std::uint16_t;
using Uint32 = std::uint32_t;
using Sint32 = std::int32_t;
using SDL_AudioDeviceID = Uint32;
using SDL_AudioCallback = void(*)(void*,Uint8*,int);

struct SDL_AudioSpec {
    int freq; Uint16 format; Uint8 channels, silence; Uint16 samples, padding;
    Uint32 size; SDL_AudioCallback callback; void* userdata;
};

struct SDL_Keysym {
    Sint32 scancode;
    Sint32 sym;
    Uint16 mod;
    Uint32 unused;
};

struct SDL_KeyboardEvent {
    Uint32 type, timestamp, windowID;
    Uint8 state, repeat, padding2, padding3;
    SDL_Keysym keysym;
};

struct SDL_MouseMotionEvent {
    Uint32 type, timestamp, windowID, which, state;
    Sint32 x, y, xrel, yrel;
};

struct SDL_WindowEvent {
    Uint32 type, timestamp, windowID;
    Uint8 event, padding1, padding2, padding3;
    Sint32 data1, data2;
};

struct SDL_MouseButtonEvent {
    Uint32 type, timestamp, windowID, which;
    Uint8 button, state, clicks, padding1;
    Sint32 x, y;
};

struct SDL_MouseWheelEvent {
    Uint32 type, timestamp, windowID, which;
    Sint32 x, y;
    Uint32 direction;
};

union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_WindowEvent window;
    std::uint8_t padding[56];
};

int SDL_Init(Uint32 flags);
void SDL_Quit();
const char* SDL_GetError();
int SDL_GL_SetAttribute(int attr, int value);
SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window* window);
SDL_GLContext SDL_GL_CreateContext(SDL_Window* window);
void SDL_GL_DeleteContext(SDL_GLContext context);
int SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext context);
int SDL_GL_SetSwapInterval(int interval);
void SDL_GL_SwapWindow(SDL_Window* window);
void* SDL_GL_GetProcAddress(const char* proc);
int SDL_PollEvent(SDL_Event* event);
const Uint8* SDL_GetKeyboardState(int* count);
int SDL_SetRelativeMouseMode(int enabled);
Uint32 SDL_GetTicks();
void SDL_Delay(Uint32 ms);
void SDL_SetWindowTitle(SDL_Window* window, const char* title);
void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h);
int SDL_SetWindowFullscreen(SDL_Window* window, Uint32 flags);
int SDL_ShowCursor(int toggle);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device,int capture,const SDL_AudioSpec* desired,SDL_AudioSpec* obtained,int allowedChanges);
void SDL_CloseAudioDevice(SDL_AudioDeviceID device);
void SDL_PauseAudioDevice(SDL_AudioDeviceID device,int pauseOn);
int SDL_QueueAudio(SDL_AudioDeviceID device,const void* data,Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID device);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID device);

}

constexpr Uint32 SDL_INIT_VIDEO = 0x00000020u;
constexpr Uint32 SDL_INIT_TIMER = 0x00000001u;
constexpr Uint32 SDL_INIT_AUDIO = 0x00000010u;
constexpr Uint32 SDL_WINDOW_OPENGL = 0x00000002u;
constexpr Uint32 SDL_WINDOW_RESIZABLE = 0x00000020u;
constexpr Uint32 SDL_WINDOW_ALLOW_HIGHDPI = 0x00002000u;
constexpr int SDL_WINDOWPOS_CENTERED = 0x2FFF0000u;
constexpr Uint16 AUDIO_F32SYS = 0x8120u;
constexpr Uint32 SDL_QUIT = 0x100u;
constexpr Uint32 SDL_WINDOWEVENT = 0x200u;
constexpr Uint32 SDL_KEYDOWN = 0x300u;
constexpr Uint32 SDL_KEYUP = 0x301u;
constexpr Uint32 SDL_MOUSEMOTION = 0x400u;
constexpr Uint32 SDL_MOUSEBUTTONDOWN = 0x401u;
constexpr Uint32 SDL_MOUSEBUTTONUP = 0x402u;
constexpr Uint32 SDL_MOUSEWHEEL = 0x403u;
constexpr Uint8 SDL_WINDOWEVENT_RESIZED = 0x05u;
constexpr Uint8 SDL_WINDOWEVENT_SIZE_CHANGED = 0x06u;
constexpr Uint8 SDL_BUTTON_LEFT = 1;
constexpr Uint8 SDL_BUTTON_RIGHT = 3;
constexpr Uint32 SDL_WINDOW_FULLSCREEN_DESKTOP = 0x00001001u;

constexpr int SDL_GL_CONTEXT_MAJOR_VERSION = 17;
constexpr int SDL_GL_CONTEXT_MINOR_VERSION = 18;
constexpr int SDL_GL_CONTEXT_PROFILE_MASK = 21;
constexpr int SDL_GL_CONTEXT_PROFILE_CORE = 1;
constexpr int SDL_GL_DOUBLEBUFFER = 5;
constexpr int SDL_GL_DEPTH_SIZE = 6;

// SDL2 scancodes used by the game. These values are part of SDL's stable ABI.
constexpr int SC_A = 4, SC_C = 6, SC_D = 7, SC_E = 8, SC_F = 9, SC_G = 10, SC_M = 16, SC_Q = 20;
constexpr int SC_R = 21, SC_S = 22, SC_W = 26;
constexpr int SC_0 = 39, SC_1 = 30, SC_2 = 31, SC_3 = 32, SC_4 = 33;
constexpr int SC_5 = 34, SC_6 = 35, SC_7 = 36, SC_8 = 37, SC_9 = 38;
constexpr int SC_ESCAPE = 41, SC_TAB = 43, SC_LSHIFT = 225;
constexpr int SC_LCTRL = 224;
constexpr int SC_RETURN = 40, SC_SPACE = 44, SC_RIGHT = 79, SC_LEFT = 80;
constexpr int SC_DOWN = 81, SC_UP = 82;
