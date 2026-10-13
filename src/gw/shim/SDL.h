// Shim: only the SDL *types* that Cannonball's sdl2/*.hpp headers mention. No SDL code is called.
#pragma once
#include <cstdint>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int16_t Sint16; typedef int32_t Sint32;
typedef uint32_t SDL_AudioDeviceID;
struct SDL_Keysym { int sym; };
struct SDL_JoyAxisEvent { int axis; int value; };
struct SDL_JoyButtonEvent { int button; };
struct SDL_JoyHatEvent { int value; };
struct SDL_ControllerAxisEvent { int axis; int value; };
struct SDL_ControllerButtonEvent { int button; };
struct SDL_Joystick; struct SDL_GameController; struct SDL_Haptic;
struct SDL_Surface; struct SDL_Window; struct SDL_Renderer; struct SDL_Texture;
struct SDL_Rect { int x, y, w, h; };
typedef int SDL_GameControllerAxis; typedef int SDL_GameControllerButton;
