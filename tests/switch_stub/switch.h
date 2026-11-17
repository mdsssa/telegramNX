#pragma once
// Minimal libnx-shaped stub used only to syntax-check the Switch translation
// units on a host compiler. Not used by any real build.
#include <cstddef>
#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;
using s64 = int64_t;
using Result = int;
#define R_FAILED(x) ((x) != 0)
#define R_SUCCEEDED(x) ((x) == 0)

struct NWindow { int dummy; };
struct Framebuffer { int dummy; };
constexpr u32 PIXEL_FORMAT_BGRA_8888 = 4;

struct Thread { void* handle; };
struct Mutex { int lock; };
using ThreadFunc = void (*)(void*);
Result threadCreate(Thread*, ThreadFunc, void*, void*, size_t, int, int);
Result threadStart(Thread*);
Result threadWaitForExit(Thread*);
Result threadClose(Thread*);
void mutexInit(Mutex*);
void mutexLock(Mutex*);
void mutexUnlock(Mutex*);
void svcSleepThread(s64);
void randomGet(void*, size_t);

Result socketInitializeDefault();
void socketExit();
NWindow* nwindowGetDefault();
Result framebufferCreate(Framebuffer*, NWindow*, u32, u32, u32, u32);
Result framebufferMakeLinear(Framebuffer*);
void* framebufferBegin(Framebuffer*, u32*);
void framebufferEnd(Framebuffer*);
void framebufferClose(Framebuffer*);

struct PadState { int dummy; };
void padInitializeDefault(PadState*);
void padUpdate(PadState*);
u64 padGetButtonsDown(const PadState*);
constexpr u64 HidNpadButton_A = 1;
constexpr u64 HidNpadButton_B = 2;
constexpr u64 HidNpadButton_X = 4;
constexpr u64 HidNpadButton_Y = 8;
constexpr u64 HidNpadButton_Up = 16;
constexpr u64 HidNpadButton_Down = 32;
constexpr u64 HidNpadButton_Left = 64;
constexpr u64 HidNpadButton_Right = 128;

struct HidTouchState {
    u64 delta_time;
    u32 attributes;
    u32 finger_id;
    u32 x;
    u32 y;
    u32 diameter_x;
    u32 diameter_y;
    u32 rotation_angle;
    u32 reserved;
};
struct HidTouchScreenState {
    u64 sampling_number;
    s32 count;
    u32 reserved;
    HidTouchState touches[16];
};
void hidInitializeTouchScreen();
size_t hidGetTouchScreenStates(HidTouchScreenState*, size_t);

struct SwkbdConfig { int dummy; };
Result swkbdCreate(SwkbdConfig*, s32);
void swkbdClose(SwkbdConfig*);
void swkbdConfigMakePresetDefault(SwkbdConfig*);
void swkbdConfigSetGuideText(SwkbdConfig*, const char*);
void swkbdConfigSetInitialText(SwkbdConfig*, const char*);
void swkbdConfigSetStringLenMax(SwkbdConfig*, u32);
void swkbdConfigSetPasswordFlag(SwkbdConfig*, u32);
Result swkbdShow(SwkbdConfig*, char*, size_t);

struct PrintConsole { int dummy; };
PrintConsole* consoleInit(PrintConsole*);
void consoleExit(PrintConsole*);
bool appletMainLoop();
