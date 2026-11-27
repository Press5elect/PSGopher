/*
 * PSGopher - the SDL3 and SDL3_ttf functions Gopher64 calls, on the PS5 (docs/dev/architecture.md D2).
 *
 * Gopher64 keeps calling SDL through the real sdl3-sys bindings (SDL 3.4.20, built with no-link);
 * this file defines exactly those functions (llvm-nm -u of libgopher64.a: 88), with SDL's
 * documented behaviour, over the PS5 layer (ps5/host/platform.c: pads, sound, klog) and the RADV
 * linked into the title (VK_KHR_display). It is compiled against SDL's own headers, so each
 * definition is checked against SDL's declaration.
 *
 * What exists on the console: one display (the "window"), up to four DualSense controllers (one per
 * signed-in user), one 48 kHz stereo output. No keyboard, no mouse, no SDL renderer: the calls
 * only Gopher64's desktop input-profile editor makes fail with SDL_GetError() set, never through a
 * NULL pointer.
 *
 * Threads: SDL_PumpEvents, the event watches and the timers run on the emulation thread (the
 * thread that first initialised a subsystem); the audio stream is filled by the PS5 layer's
 * audio thread and guarded by the stream's lock, as SDL's is.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_ttf/SDL_ttf.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

extern "C" {
#include "platform.h"
#include "psgopher_sdl3.h"
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
}

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

/* ------------------------------------------------------------------ errors, hints, memory */

static thread_local std::string sdl_error;

static bool set_error(const char *format, ...)
{
   char buffer[256];
   va_list args;
   va_start(args, format);
   vsnprintf(buffer, sizeof(buffer), format, args);
   va_end(args);
   sdl_error = buffer;
   return false;
}

static void unsupported(const char *name)
{
   set_error("%s: not on the PS5", name);
}

extern "C" const char *SDL_GetError(void) { return sdl_error.c_str(); }
extern "C" bool SDL_SetHint(const char *, const char *) { return true; }
extern "C" void SDL_free(void *memory) { free(memory); }

/* ------------------------------------------------------------------ initialisation */

static std::atomic<SDL_InitFlags> initialised{0};
static std::thread::id main_thread;
static std::once_flag main_thread_once;
static bool pads_open;

static bool on_main_thread() { return std::this_thread::get_id() == main_thread; }

extern "C" bool SDL_InitSubSystem(SDL_InitFlags flags)
{
   std::call_once(main_thread_once, [] { main_thread = std::this_thread::get_id(); });
   if ((flags & (SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) && !pads_open) {
      pads_open = true;
      if (!pad_open())
         say("sdl3: no controller yet (users who sign in later get theirs)");
   }
   initialised |= flags;
   return true;
}

extern "C" SDL_InitFlags SDL_WasInit(SDL_InitFlags flags)
{
   const SDL_InitFlags have = initialised.load();
   return flags ? (have & flags) : have;
}

extern "C" void SDL_Quit(void) { initialised = 0; }

/* ------------------------------------------------------------------ the window (the display) */

struct SDL_Window {
   int width = 3840, height = 2160; /* the console's output until the display reports its mode */
};
static SDL_Window the_window;
static bool window_made;

extern "C" SDL_Window *SDL_CreateWindow(const char *, int, int, SDL_WindowFlags)
{
   if (window_made)
      return (SDL_Window *)(set_error("SDL_CreateWindow: the PS5 has one display"), nullptr);
   window_made = true;
   return &the_window;
}
extern "C" void SDL_DestroyWindow(SDL_Window *) { window_made = false; }
extern "C" bool SDL_ShowWindow(SDL_Window *) { return true; }
extern "C" bool SDL_SyncWindow(SDL_Window *) { return true; }
extern "C" bool SDL_HideCursor(void) { return true; }
extern "C" bool SDL_SetWindowFullscreen(SDL_Window *, bool) { return true; }
extern "C" SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *)
{
   return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN | SDL_WINDOW_INPUT_FOCUS;
}
extern "C" bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{
   if (!window)
      window = &the_window;
   if (w)
      *w = window->width;
   if (h)
      *h = window->height;
   return true;
}
/* Gopher64 sizes its on-screen text by this; a 1080p screen is SDL's 1.0 */
extern "C" float SDL_GetWindowDisplayScale(SDL_Window *window)
{
   return (window ? window : &the_window)->height / 1080.0f;
}
extern "C" bool SDL_GetWindowSafeArea(SDL_Window *window, SDL_Rect *rect)
{
   if (!window)
      window = &the_window;
   if (rect)
      *rect = SDL_Rect{0, 0, window->width, window->height};
   return true;
}

/* The SDL renderer: only the desktop input-profile editor draws with it */
extern "C" bool SDL_CreateWindowAndRenderer(const char *, int, int, SDL_WindowFlags, SDL_Window **window,
                                            SDL_Renderer **renderer)
{
   if (window)
      *window = nullptr;
   if (renderer)
      *renderer = nullptr;
   unsupported("SDL_CreateWindowAndRenderer");
   return false;
}
extern "C" void SDL_DestroyRenderer(SDL_Renderer *) {}
extern "C" bool SDL_RenderClear(SDL_Renderer *) { return false; }
extern "C" bool SDL_RenderFillRect(SDL_Renderer *, const SDL_FRect *) { return false; }
extern "C" bool SDL_RenderPresent(SDL_Renderer *) { return false; }
extern "C" bool SDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8) { return false; }

/* ------------------------------------------------------------------ Vulkan: RADV, VK_KHR_display */

extern "C" SDL_FunctionPointer SDL_Vulkan_GetVkGetInstanceProcAddr(void)
{
   return (SDL_FunctionPointer)vk_icdGetInstanceProcAddr;
}

extern "C" char const *const *SDL_Vulkan_GetInstanceExtensions(Uint32 *count)
{
   static const char *const extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
   if (count)
      *count = 2;
   return extensions;
}

/* The display's mode: at 60 Hz (PS5_VIDEOOUT_59HZ, the title's default) the fastest up to 60.5 Hz, the
 * N64's 59.94; at 120 Hz the fastest the driver offers (D5, D14) */
extern "C" bool SDL_Vulkan_CreateSurface(SDL_Window *window, VkInstance instance,
                                         const struct VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
   if (!window)
      window = &the_window;
#define PSG_VK(name) auto name = (PFN_##name)vk_icdGetInstanceProcAddr(instance, #name)
   PSG_VK(vkEnumeratePhysicalDevices);
   PSG_VK(vkGetPhysicalDeviceDisplayPropertiesKHR);
   PSG_VK(vkGetDisplayModePropertiesKHR);
   PSG_VK(vkCreateDisplayPlaneSurfaceKHR);
#undef PSG_VK
   if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceDisplayPropertiesKHR || !vkGetDisplayModePropertiesKHR ||
       !vkCreateDisplayPlaneSurfaceKHR)
      return set_error("SDL_Vulkan_CreateSurface: VK_KHR_display is not enabled");
   uint32_t gpus = 1;
   VkPhysicalDevice gpu = VK_NULL_HANDLE;
   if (vkEnumeratePhysicalDevices(instance, &gpus, &gpu) < 0 || gpus == 0)
      return set_error("SDL_Vulkan_CreateSurface: no GPU");
   uint32_t displays = 1;
   VkDisplayPropertiesKHR display = {};
   if (vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &displays, &display) < 0 || displays == 0)
      return set_error("SDL_Vulkan_CreateSurface: no display");
   uint32_t count = 0;
   vkGetDisplayModePropertiesKHR(gpu, display.display, &count, nullptr);
   std::vector<VkDisplayModePropertiesKHR> modes(count);
   vkGetDisplayModePropertiesKHR(gpu, display.display, &count, modes.data());
   if (count == 0)
      return set_error("SDL_Vulkan_CreateSurface: the display has no mode");
   const bool sixty = getenv("PS5_VIDEOOUT_59HZ") != nullptr;
   const VkDisplayModePropertiesKHR *chosen = &modes[0];
   for (const auto &mode : modes) {
      const uint32_t rate = mode.parameters.refreshRate; /* millihertz */
      if (!sixty) {
         if (rate > chosen->parameters.refreshRate)
            chosen = &mode;
      } else if (rate <= 60500 && (chosen->parameters.refreshRate > 60500 || rate > chosen->parameters.refreshRate))
         chosen = &mode;
   }
   VkDisplaySurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
   info.displayMode = chosen->displayMode;
   info.planeIndex = 0;
   info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
   info.globalAlpha = 1.0f;
   info.imageExtent = chosen->parameters.visibleRegion;
   if (vkCreateDisplayPlaneSurfaceKHR(instance, &info, allocator, surface) != VK_SUCCESS)
      return set_error("SDL_Vulkan_CreateSurface: vkCreateDisplayPlaneSurfaceKHR failed");
   window->width = (int)info.imageExtent.width;
   window->height = (int)info.imageExtent.height;
   say("sdl3: display %ux%u at %.3f Hz (%u modes)", info.imageExtent.width, info.imageExtent.height,
       chosen->parameters.refreshRate / 1000.0, count);
   return true;
}

extern "C" void SDL_Vulkan_DestroySurface(VkInstance instance, VkSurfaceKHR surface,
                                          const struct VkAllocationCallbacks *allocator)
{
   auto destroy = (PFN_vkDestroySurfaceKHR)vk_icdGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
   if (destroy && surface)
      destroy(instance, surface, allocator);
}

/* ------------------------------------------------------------------ events, timers, main thread */

struct Watch {
   SDL_EventFilter filter;
   void *userdata;
};
static std::mutex events_lock;
static std::vector<Watch> watches;

struct Timer {
   SDL_TimerID id;
   double due;
   Uint32 interval;
   SDL_TimerCallback callback;
   void *userdata;
};
static std::vector<Timer> timers;
static SDL_TimerID next_timer_id = 1;

struct MainCall {
   SDL_MainThreadCallback callback;
   void *userdata;
   bool *done; /* set (under events_lock) when the caller waits for it */
};
static std::deque<MainCall> main_calls;
static std::condition_variable main_calls_done;

/* SDL calls an event watch as the event is added; there is no event queue to read here (Gopher64
 * only watches) */
static void deliver(SDL_Event *event)
{
   std::vector<Watch> current;
   {
      std::lock_guard<std::mutex> hold(events_lock);
      current = watches;
   }
   for (const auto &watch : current)
      watch.filter(watch.userdata, event);
}

extern "C" bool SDL_AddEventWatch(SDL_EventFilter filter, void *userdata)
{
   std::lock_guard<std::mutex> hold(events_lock);
   watches.push_back({filter, userdata});
   return true;
}

extern "C" void SDL_RemoveEventWatch(SDL_EventFilter filter, void *userdata)
{
   std::lock_guard<std::mutex> hold(events_lock);
   for (auto it = watches.begin(); it != watches.end(); ++it)
      if (it->filter == filter && it->userdata == userdata) {
         watches.erase(it);
         break;
      }
}

/* how many watches are registered: the benchmark mode waits for parallel-rdp's before it sends
 * Gopher64 its fast-forward and exit events */
extern "C" int psgopher_sdl3_watch_count(void)
{
   std::lock_guard<std::mutex> hold(events_lock);
   return (int)watches.size();
}

extern "C" bool SDL_PushEvent(SDL_Event *event)
{
   if (!event)
      return set_error("SDL_PushEvent: no event");
   if (event->common.timestamp == 0)
      event->common.timestamp = (Uint64)(now_seconds() * 1e9);
   deliver(event);
   return true;
}

extern "C" bool SDL_WaitEventTimeout(SDL_Event *, Sint32 timeout_ms)
{
   /* no event queue: only the desktop input-profile editor waits on one */
   if (timeout_ms > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
   return false;
}

extern "C" bool SDL_RunOnMainThread(SDL_MainThreadCallback callback, void *userdata, bool wait_complete)
{
   if (on_main_thread()) {
      callback(userdata);
      return true;
   }
   std::unique_lock<std::mutex> hold(events_lock);
   bool done = false;
   main_calls.push_back({callback, userdata, wait_complete ? &done : nullptr});
   if (wait_complete)
      main_calls_done.wait(hold, [&] { return done; });
   return true;
}

extern "C" SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *userdata)
{
   if (!callback)
      return (SDL_TimerID)(set_error("SDL_AddTimer: no callback"), 0);
   std::lock_guard<std::mutex> hold(events_lock);
   const SDL_TimerID id = next_timer_id++;
   timers.push_back({id, now_seconds() + interval / 1000.0, interval, callback, userdata});
   return id;
}

static void run_main_calls_and_timers()
{
   for (;;) {
      MainCall call;
      {
         std::lock_guard<std::mutex> hold(events_lock);
         if (main_calls.empty())
            break;
         call = main_calls.front();
         main_calls.pop_front();
      }
      call.callback(call.userdata);
      if (call.done) {
         std::lock_guard<std::mutex> hold(events_lock);
         *call.done = true;
         main_calls_done.notify_all();
      }
   }
   const double now = now_seconds();
   std::vector<Timer> due;
   {
      std::lock_guard<std::mutex> hold(events_lock);
      for (auto it = timers.begin(); it != timers.end();) {
         if (it->due <= now) {
            due.push_back(*it);
            it = timers.erase(it);
         } else
            ++it;
      }
   }
   for (auto &timer : due) {
      const Uint32 again = timer.callback(timer.userdata, timer.id, timer.interval);
      if (again) {
         std::lock_guard<std::mutex> hold(events_lock);
         timer.interval = again;
         timer.due = now + again / 1000.0;
         timers.push_back(timer);
      }
   }
}

/* ------------------------------------------------------------------ controllers */

/* One per player of the PS5 layer (a signed-in user's DualSense). SDL's ids start at 1. */
struct SDL_Joystick {
   int player;
   int opened; /* joystick and gamepad opens, each counted */
};
struct SDL_Gamepad {
   SDL_Joystick *joystick;
};
static SDL_Joystick joysticks[PAD_PLAYERS];
static SDL_Gamepad gamepads[PAD_PLAYERS];
static pad_reading pad_now[PAD_PLAYERS];
static bool pad_connected[PAD_PLAYERS];
static double rumble_until[PAD_PLAYERS];

static SDL_JoystickID id_of(int player) { return (SDL_JoystickID)(player + 1); }
static int player_of(SDL_JoystickID id)
{
   const int player = (int)id - 1;
   return player >= 0 && player < PAD_PLAYERS && pad_connected[player] ? player : -1;
}

/* PSGopher's quick menu (ps5/host/quickmenu.cpp): a touch pad tap of any player, pressed and let go
 * within half a second with no other button pressed meanwhile (with one, it is Gopher64's hotkey) */
/* (each player's own press: another controller's reading at rest is not this one let go, FE-004) */
static bool tap_down[PAD_PLAYERS], tap_clean[PAD_PLAYERS], tap_seen;
static uint64_t tap_since_us[PAD_PLAYERS];
static void watch_tap(int player, const pad_reading *readings, int count)
{
   if (player < 0 || player >= PAD_PLAYERS)
      return;
   bool &down = tap_down[player], &clean = tap_clean[player];
   uint64_t &since_us = tap_since_us[player];
   for (int i = 0; i < count; i++) {
      const uint32_t b = readings[i].buttons;
      if (b & PAD_INTERCEPTED)
         continue;
      const bool touch = (b & PAD_TOUCH_PAD) != 0;
      if (touch && !down) {
         down = true;
         clean = (b & ~PAD_TOUCH_PAD) == 0;
         since_us = readings[i].timestamp_us;
      } else if (touch) {
         if (b & ~PAD_TOUCH_PAD)
            clean = false;
      } else if (down) {
         down = false;
         if (clean && readings[i].timestamp_us - since_us < 500000)
            tap_seen = true;
      }
   }
}

/* after the menu: the game sees the pads at rest until every button is let go (the Cross that
 * chose Resume is not the game's A) */
static bool hold_until_released;

static void poll_pads()
{
   if (!pads_open)
      return;
   struct pad first;
   pad_poll(&first);
   const uint32_t present = pad_players();
   uint32_t held_any = 0;
   for (int player = 0; player < PAD_PLAYERS; player++) {
      const bool connected = (present >> player) & 1;
      const pad_reading *readings = nullptr;
      const int count = connected ? pad_player_readings(player, &readings) : 0;
      watch_tap(player, readings, count);
      if (count > 0)
         held_any |= readings[count - 1].buttons & ~PAD_INTERCEPTED;
      if (count > 0 && hold_until_released) {
         pad_now[player] = pad_reading{};
         pad_now[player].left_x = pad_now[player].left_y = 128;
         pad_now[player].right_x = pad_now[player].right_y = 128;
         pad_now[player].connected = true;
      } else if (count > 0) {
         const pad_reading &last = readings[count - 1];
         /* while the shell has the pad (home screen, a dialog) the title sees it at rest */
         if (last.buttons & PAD_INTERCEPTED) {
            pad_now[player] = pad_reading{};
            pad_now[player].left_x = pad_now[player].left_y = 128;
            pad_now[player].right_x = pad_now[player].right_y = 128;
         } else
            pad_now[player] = last;
      }
      if (connected != pad_connected[player]) {
         pad_connected[player] = connected;
         if (!connected)
            pad_now[player] = pad_reading{};
         say("sdl3: player %d's controller %s", player + 1, connected ? "connected" : "gone");
         SDL_Event event = {};
         event.jdevice.type = connected ? SDL_EVENT_JOYSTICK_ADDED : SDL_EVENT_JOYSTICK_REMOVED;
         event.jdevice.which = id_of(player);
         SDL_PushEvent(&event);
         event.gdevice.type = connected ? SDL_EVENT_GAMEPAD_ADDED : SDL_EVENT_GAMEPAD_REMOVED;
         SDL_PushEvent(&event);
      }
      if (rumble_until[player] > 0 && now_seconds() >= rumble_until[player]) {
         rumble_until[player] = 0;
         pad_player_vibrate(player, 0, 0);
      }
   }
   if (hold_until_released && held_any == 0)
      hold_until_released = false;
}

extern "C" int psgopher_sdl3_take_tap(void)
{
   const bool seen = tap_seen;
   tap_seen = false;
   /* the quick menu's tap can be off (psgopher.ini's quickmenu_tap; the title keeps it on when no
    * hotkey leaves the game) */
   const char *on = getenv("PSGOPHER_QUICKMENU_TAP");
   if (on && !strcmp(on, "0"))
      return 0;
   return seen;
}

extern "C" void psgopher_sdl3_menu_pad(struct pad *pad)
{
   pad_poll(pad);
   const pad_reading *readings = nullptr;
   watch_tap(0, readings, pad_player_readings(0, &readings));
}

extern "C" void psgopher_sdl3_hold_until_released(void)
{
   hold_until_released = true;
   tap_seen = false;
   for (bool &down : tap_down)
      down = false;
}

extern "C" void SDL_PumpEvents(void)
{
   if (!on_main_thread())
      return;
   poll_pads();
   run_main_calls_and_timers();
}

extern "C" SDL_JoystickID *SDL_GetJoysticks(int *count)
{
   SDL_JoystickID *ids = (SDL_JoystickID *)calloc(PAD_PLAYERS + 1, sizeof(SDL_JoystickID));
   int n = 0;
   for (int player = 0; player < PAD_PLAYERS; player++)
      if (pad_connected[player])
         ids[n++] = id_of(player);
   if (count)
      *count = n;
   return ids;
}

extern "C" const char *SDL_GetJoystickNameForID(SDL_JoystickID id)
{
   return player_of(id) >= 0 ? "DualSense Wireless Controller" : nullptr;
}

extern "C" const char *SDL_GetJoystickPathForID(SDL_JoystickID id)
{
   static const char *const paths[PAD_PLAYERS] = {"ps5:pad1", "ps5:pad2", "ps5:pad3", "ps5:pad4"};
   const int player = player_of(id);
   return player >= 0 ? paths[player] : (set_error("no controller %u", (unsigned)id), nullptr);
}

/* SDL's GUID layout for a USB device: bus, CRC, vendor, product, version (all little-endian) */
static SDL_GUID dualsense_guid()
{
   SDL_GUID guid = {};
   const uint16_t words[8] = {0x0003 /* USB */, 0, 0x054c /* Sony */, 0, 0x0ce6 /* DualSense */, 0, 0, 0};
   memcpy(guid.data, words, sizeof(words));
   return guid;
}

extern "C" SDL_GUID SDL_GetJoystickGUIDForID(SDL_JoystickID id)
{
   return player_of(id) >= 0 ? dualsense_guid() : SDL_GUID{};
}
extern "C" SDL_GUID SDL_GetGamepadGUIDForID(SDL_JoystickID id) { return SDL_GetJoystickGUIDForID(id); }

extern "C" SDL_Joystick *SDL_OpenJoystick(SDL_JoystickID id)
{
   const int player = player_of(id);
   if (player < 0)
      return (SDL_Joystick *)(set_error("SDL_OpenJoystick: no controller %u", (unsigned)id), nullptr);
   joysticks[player].player = player;
   joysticks[player].opened++;
   return &joysticks[player];
}

extern "C" SDL_Joystick *SDL_GetJoystickFromID(SDL_JoystickID id)
{
   const int player = (int)id - 1;
   if (player < 0 || player >= PAD_PLAYERS || joysticks[player].opened == 0)
      return nullptr;
   return &joysticks[player];
}

extern "C" void SDL_CloseJoystick(SDL_Joystick *joystick)
{
   if (joystick && joystick->opened > 0)
      joystick->opened--;
}

extern "C" SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id)
{
   SDL_Joystick *joystick = SDL_OpenJoystick(id);
   if (!joystick)
      return nullptr;
   gamepads[joystick->player].joystick = joystick;
   return &gamepads[joystick->player];
}

extern "C" SDL_Gamepad *SDL_GetGamepadFromID(SDL_JoystickID id)
{
   SDL_Joystick *joystick = SDL_GetJoystickFromID(id);
   return joystick && gamepads[joystick->player].joystick ? &gamepads[joystick->player] : nullptr;
}

extern "C" void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
   if (gamepad && gamepad->joystick) {
      SDL_CloseJoystick(gamepad->joystick);
      if (gamepad->joystick->opened == 0)
         gamepad->joystick = nullptr;
   }
}

static Sint16 stick(uint8_t value) /* 0..255, 128 at rest -> SDL's -32768..32767 */
{
   const int v = ((int)value - 128) * 256 + ((int)value - 128) * 2;
   return (Sint16)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}
static Sint16 trigger(uint8_t value) { return (Sint16)((int)value * 32767 / 255); }

/* The raw joystick: axes left x/y, right x/y, L2, R2 (y down-positive, as SDL's) */
extern "C" Sint16 SDL_GetJoystickAxis(SDL_Joystick *joystick, int axis)
{
   if (!joystick)
      return 0;
   const pad_reading &p = pad_now[joystick->player];
   switch (axis) {
   case 0: return stick(p.left_x);
   case 1: return stick(p.left_y);
   case 2: return stick(p.right_x);
   case 3: return stick(p.right_y);
   case 4: return trigger(p.l2);
   case 5: return trigger(p.r2);
   default: return 0;
   }
}

extern "C" bool SDL_GetJoystickAxisInitialState(SDL_Joystick *joystick, int axis, Sint16 *state)
{
   if (state)
      *state = 0; /* sticks at rest, triggers released */
   return joystick && axis >= 0 && axis < 6;
}

/* The raw joystick's buttons, in SDL's DualSense (HIDAPI) order */
static const uint32_t joystick_buttons[] = {PAD_CROSS,  PAD_CIRCLE, PAD_SQUARE,   PAD_TRIANGLE,
                                            PAD_TOUCH_PAD, 0 /* PS */, PAD_OPTIONS, PAD_L3,
                                            PAD_R3,     PAD_L1,     PAD_R1};

extern "C" bool SDL_GetJoystickButton(SDL_Joystick *joystick, int button)
{
   if (!joystick || button < 0 || button >= (int)(sizeof(joystick_buttons) / sizeof(joystick_buttons[0])))
      return false;
   return pad_now[joystick->player].buttons & joystick_buttons[button];
}

extern "C" Uint8 SDL_GetJoystickHat(SDL_Joystick *joystick, int hat)
{
   if (!joystick || hat != 0)
      return SDL_HAT_CENTERED;
   const uint32_t b = pad_now[joystick->player].buttons;
   return (Uint8)(((b & PAD_UP) ? SDL_HAT_UP : 0) | ((b & PAD_RIGHT) ? SDL_HAT_RIGHT : 0) |
                  ((b & PAD_DOWN) ? SDL_HAT_DOWN : 0) | ((b & PAD_LEFT) ? SDL_HAT_LEFT : 0));
}

/* SDL's standard gamepad over the DualSense: Cross = South, Circle = East, Square = West,
 * Triangle = North, Options = Start, the touch pad click = Back (the Create button is the shell's) */
extern "C" bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
   if (!gamepad || !gamepad->joystick)
      return false;
   const uint32_t b = pad_now[gamepad->joystick->player].buttons;
   switch (button) {
   case SDL_GAMEPAD_BUTTON_SOUTH: return b & PAD_CROSS;
   case SDL_GAMEPAD_BUTTON_EAST: return b & PAD_CIRCLE;
   case SDL_GAMEPAD_BUTTON_WEST: return b & PAD_SQUARE;
   case SDL_GAMEPAD_BUTTON_NORTH: return b & PAD_TRIANGLE;
   case SDL_GAMEPAD_BUTTON_BACK: return b & PAD_TOUCH_PAD;
   case SDL_GAMEPAD_BUTTON_START: return b & PAD_OPTIONS;
   case SDL_GAMEPAD_BUTTON_LEFT_STICK: return b & PAD_L3;
   case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return b & PAD_R3;
   case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return b & PAD_L1;
   case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return b & PAD_R1;
   case SDL_GAMEPAD_BUTTON_DPAD_UP: return b & PAD_UP;
   case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return b & PAD_DOWN;
   case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return b & PAD_LEFT;
   case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return b & PAD_RIGHT;
   case SDL_GAMEPAD_BUTTON_TOUCHPAD: return b & PAD_TOUCH_PAD;
   default: return false;
   }
}

extern "C" Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
   if (!gamepad || !gamepad->joystick)
      return 0;
   const pad_reading &p = pad_now[gamepad->joystick->player];
   switch (axis) {
   case SDL_GAMEPAD_AXIS_LEFTX: return stick(p.left_x);
   case SDL_GAMEPAD_AXIS_LEFTY: return stick(p.left_y);
   case SDL_GAMEPAD_AXIS_RIGHTX: return stick(p.right_x);
   case SDL_GAMEPAD_AXIS_RIGHTY: return stick(p.right_y);
   case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return trigger(p.l2);
   case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return trigger(p.r2);
   default: return 0;
   }
}

static bool rumble(SDL_Joystick *joystick, Uint16 low, Uint16 high, Uint32 duration_ms)
{
   if (!joystick)
      return set_error("rumble: no controller");
   pad_player_vibrate(joystick->player, low / 65535.0f, high / 65535.0f);
   const bool stop = low == 0 && high == 0;
   rumble_until[joystick->player] =
      stop || duration_ms == SDL_HAPTIC_INFINITY ? 0 : now_seconds() + duration_ms / 1000.0;
   return true;
}
extern "C" bool SDL_RumbleJoystick(SDL_Joystick *joystick, Uint16 low, Uint16 high, Uint32 duration_ms)
{
   return rumble(joystick, low, high, duration_ms);
}
extern "C" bool SDL_RumbleGamepad(SDL_Gamepad *gamepad, Uint16 low, Uint16 high, Uint32 duration_ms)
{
   return rumble(gamepad ? gamepad->joystick : nullptr, low, high, duration_ms);
}

extern "C" const char *SDL_GetGamepadStringForButton(SDL_GamepadButton button)
{
   static const char *const names[] = {"a",          "b",          "x",            "y",           "back",
                                       "guide",      "start",      "leftstick",    "rightstick",  "leftshoulder",
                                       "rightshoulder", "dpup",   "dpdown",       "dpleft",      "dpright",
                                       "misc1",      "paddle1",    "paddle2",      "paddle3",     "paddle4",
                                       "touchpad"};
   return button >= 0 && button < (int)(sizeof(names) / sizeof(names[0])) ? names[button] : nullptr;
}

extern "C" const char *SDL_GetGamepadStringForAxis(SDL_GamepadAxis axis)
{
   static const char *const names[] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
   return axis >= 0 && axis < (int)(sizeof(names) / sizeof(names[0])) ? names[axis] : nullptr;
}

/* ------------------------------------------------------------------ keyboard: none */

extern "C" const bool *SDL_GetKeyboardState(int *numkeys)
{
   static const bool keys[SDL_SCANCODE_COUNT] = {};
   if (numkeys)
      *numkeys = SDL_SCANCODE_COUNT;
   return keys;
}
extern "C" bool SDL_HasKeyboard(void) { return false; }
extern "C" const char *SDL_GetScancodeName(SDL_Scancode) { return ""; }

/* ------------------------------------------------------------------ audio: one stream to 48 kHz */

/* A windowed-sinc resampler (Kaiser window, 16 taps, 256 phases) from the stream's rate times its
 * frequency ratio to the output's 48 kHz, with SDL_AudioStream's accounting: Queued is the input
 * not consumed yet, Available the output that can be made from it now (D6). */
static constexpr int TAPS = 16, PHASES = 256, OUTPUT_RATE = 48000;

struct SDL_AudioStream {
   std::recursive_mutex lock;
   int rate = 44100;
   float ratio = 1.0f;
   float gain = 1.0f;
   std::vector<float> input; /* stereo frames, interleaved, from `start` on */
   size_t start = 0;         /* first frame still needed (the history the filter reads) */
   double position = 0;      /* the next output's place, in input frames from `start` */
   std::vector<float> table; /* PHASES+1 rows of TAPS */
   double table_cutoff = 0;
   bool playing = false;
   bool fed = false; /* the game has given audio: an empty queue before that is no underrun */
   bool held = false; /* the game is paused (the quick menu): nobody feeds the stream, so no underrun counts */
   psgopher_sdl3_audio_stats stats = {};
};
static SDL_AudioStream *the_stream;

static double bessel_i0(double x)
{
   double sum = 1, term = 1;
   for (int k = 1; k < 32; k++) {
      term *= (x / (2 * k)) * (x / (2 * k));
      sum += term;
   }
   return sum;
}

static void make_table(SDL_AudioStream *s, double cutoff)
{
   const double beta = 8.0;
   s->table.assign((PHASES + 1) * TAPS, 0.0f);
   for (int phase = 0; phase <= PHASES; phase++) {
      double sum = 0;
      float *row = &s->table[phase * TAPS];
      for (int tap = 0; tap < TAPS; tap++) {
         /* tap t sits at input frame (base - TAPS/2 + 1 + t); the output is at base + phase/PHASES */
         const double x = (tap - (TAPS / 2 - 1)) - (double)phase / PHASES;
         const double w = x / (TAPS / 2);
         const double window = std::fabs(w) >= 1 ? 0 : bessel_i0(beta * std::sqrt(1 - w * w)) / bessel_i0(beta);
         const double sinc = x == 0 ? 1 : std::sin(M_PI * cutoff * x) / (M_PI * cutoff * x);
         row[tap] = (float)(cutoff * sinc * window);
         sum += row[tap];
      }
      for (int tap = 0; tap < TAPS; tap++) /* unity gain at DC for every phase */
         row[tap] = (float)(row[tap] / sum);
   }
   s->table_cutoff = cutoff;
}

static double step_of(const SDL_AudioStream *s) { return (double)s->rate * s->ratio / OUTPUT_RATE; }

/* input frames held from `start` */
static size_t held(const SDL_AudioStream *s) { return s->input.size() / 2; }

static int available_frames(const SDL_AudioStream *s)
{
   /* an output at `position` needs frames up to floor(position) + TAPS/2 */
   const double last = (double)held(s) - TAPS / 2 - 1;
   if (last < s->position)
      return 0;
   return (int)((last - s->position) / step_of(s)) + 1;
}

/* Makes `frames` frames at 48 kHz (silence for what the stream does not have) */
extern "C" void psgopher_sdl3_audio_render(SDL_AudioStream *s, int16_t *out, int frames)
{
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   const double cutoff = std::min(1.0, (double)OUTPUT_RATE / (s->rate * s->ratio)) * 0.94;
   if (s->table.empty() || std::fabs(cutoff - s->table_cutoff) > 0.01)
      make_table(s, cutoff);
   const double step = step_of(s);
   int made = 0;
   if (s->playing) {
      const int ready = std::min(frames, available_frames(s));
      for (; made < ready; made++) {
         const size_t base = (size_t)s->position;
         const double fraction = s->position - base;
         const float *row = &s->table[(size_t)(fraction * PHASES + 0.5) * TAPS];
         float left = 0, right = 0;
         const ptrdiff_t first = (ptrdiff_t)base - (TAPS / 2 - 1);
         for (int tap = 0; tap < TAPS; tap++) {
            const ptrdiff_t frame = first + tap;
            if (frame < 0)
               continue; /* before the first frame: silence */
            left += row[tap] * s->input[frame * 2];
            right += row[tap] * s->input[frame * 2 + 1];
         }
         left *= s->gain * 32767.0f;
         right *= s->gain * 32767.0f;
         out[made * 2] = (int16_t)std::lrint(std::fmax(-32768.0f, std::fmin(32767.0f, left)));
         out[made * 2 + 1] = (int16_t)std::lrint(std::fmax(-32768.0f, std::fmin(32767.0f, right)));
         s->position += step;
      }
      /* drop what the filter no longer reaches */
      const size_t keep_from = (size_t)std::max(0.0, std::floor(s->position) - (TAPS / 2 - 1));
      if (keep_from > 4096) {
         s->input.erase(s->input.begin(), s->input.begin() + keep_from * 2);
         s->position -= keep_from;
         s->start += keep_from;
      }
   }
   if (made < frames) {
      memset(out + made * 2, 0, (size_t)(frames - made) * 4);
      if (s->playing && s->fed && !s->held) {
         s->stats.underrun_grains++;
         s->stats.silent_frames += (uint64_t)(frames - made);
      }
   }
   s->stats.grains++;
}

extern "C" void psgopher_sdl3_audio_hold(int held)
{
   if (!the_stream)
      return;
   std::lock_guard<std::recursive_mutex> hold(the_stream->lock);
   the_stream->held = held != 0;
}

extern "C" void psgopher_sdl3_audio_stats_read(psgopher_sdl3_audio_stats *stats)
{
   if (!the_stream) {
      *stats = {};
      return;
   }
   std::lock_guard<std::recursive_mutex> hold(the_stream->lock);
   *stats = the_stream->stats;
   stats->queued_frames = (uint64_t)std::max(0.0, held(the_stream) - the_stream->position);
}

static void audio_fill(int16_t *frames, int count, void *user)
{
   psgopher_sdl3_audio_render((SDL_AudioStream *)user, frames, count);
}

static bool spec_ok(const SDL_AudioSpec *spec)
{
   return spec && spec->format == SDL_AUDIO_S16LE && spec->channels == 2 && spec->freq > 0;
}

extern "C" SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
                                                      SDL_AudioStreamCallback callback, void *)
{
   if (device != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK || callback || !spec_ok(spec) || the_stream)
      return (SDL_AudioStream *)(set_error("SDL_OpenAudioDeviceStream: one S16 stereo playback stream only"),
                                 nullptr);
   SDL_AudioStream *s = new SDL_AudioStream;
   s->rate = spec->freq;
   the_stream = s;
   return s; /* paused until SDL_ResumeAudioStreamDevice, as SDL's */
}

extern "C" bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_ResumeAudioStreamDevice: no stream");
   {
      std::lock_guard<std::recursive_mutex> hold(s->lock);
      if (s->playing)
         return true;
      s->playing = true;
   }
   if (!audio_start(audio_fill, s))
      say("sdl3: no audio output; the stream is consumed silently");
   return true;
}

extern "C" void SDL_DestroyAudioStream(SDL_AudioStream *s)
{
   if (!s)
      return;
   audio_stop();
   if (the_stream == s)
      the_stream = nullptr;
   delete s;
}

extern "C" bool SDL_LockAudioStream(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_LockAudioStream: no stream");
   s->lock.lock();
   return true;
}

extern "C" bool SDL_UnlockAudioStream(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_UnlockAudioStream: no stream");
   s->lock.unlock();
   return true;
}

extern "C" bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buffer, int length)
{
   if (!s || length < 0 || (length && !buffer) || length % 4)
      return set_error("SDL_PutAudioStreamData: bad arguments");
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   const int16_t *samples = (const int16_t *)buffer;
   const size_t n = (size_t)length / 2;
   const size_t at = s->input.size();
   if (n)
      s->fed = true;
   s->input.resize(at + n);
   for (size_t i = 0; i < n; i++)
      s->input[at + i] = samples[i] * (1.0f / 32768.0f);
   return true;
}

extern "C" int SDL_GetAudioStreamQueued(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_GetAudioStreamQueued: no stream"), -1;
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   const double frames = held(s) - std::floor(s->position);
   return frames > 0 ? (int)frames * 4 : 0;
}

extern "C" int SDL_GetAudioStreamAvailable(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_GetAudioStreamAvailable: no stream"), -1;
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   return available_frames(s) * 4;
}

extern "C" bool SDL_ClearAudioStream(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_ClearAudioStream: no stream");
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   s->input.clear();
   s->position = 0;
   s->start = 0;
   return true;
}

extern "C" bool SDL_SetAudioStreamFormat(SDL_AudioStream *s, const SDL_AudioSpec *src, const SDL_AudioSpec *dst)
{
   if (!s || (src && !spec_ok(src)) || (dst && !(dst->freq == OUTPUT_RATE && spec_ok(dst))))
      return set_error("SDL_SetAudioStreamFormat: S16 stereo in, the 48 kHz output out");
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   if (src)
      s->rate = src->freq;
   return true;
}

extern "C" bool SDL_SetAudioStreamFrequencyRatio(SDL_AudioStream *s, float ratio)
{
   if (!s || !(ratio >= 0.01f && ratio <= 100.0f))
      return set_error("SDL_SetAudioStreamFrequencyRatio: ratio out of range");
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   s->ratio = ratio;
   return true;
}

extern "C" float SDL_GetAudioStreamGain(SDL_AudioStream *s)
{
   if (!s)
      return set_error("SDL_GetAudioStreamGain: no stream"), -1.0f;
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   return s->gain;
}

extern "C" bool SDL_SetAudioStreamGain(SDL_AudioStream *s, float gain)
{
   if (!s || gain < 0)
      return set_error("SDL_SetAudioStreamGain: bad gain");
   std::lock_guard<std::recursive_mutex> hold(s->lock);
   s->gain = gain;
   return true;
}

/* ------------------------------------------------------------------ memory streams, surfaces */

struct SDL_IOStream {
   const unsigned char *data;
   size_t size;
};

extern "C" SDL_IOStream *SDL_IOFromConstMem(const void *memory, size_t size)
{
   if (!memory)
      return (SDL_IOStream *)(set_error("SDL_IOFromConstMem: no memory"), nullptr);
   return new SDL_IOStream{(const unsigned char *)memory, size};
}

extern "C" void SDL_DestroySurface(SDL_Surface *surface)
{
   if (!surface)
      return;
   free(surface->pixels);
   free(surface);
}

/* ------------------------------------------------------------------ fonts: stb_truetype */

struct TTF_Font {
   stbtt_fontinfo info;
   SDL_IOStream *io; /* closed with the font when asked */
   float size;
   float scale;
   int ascent, descent, line_gap;
};

static void font_set_size(TTF_Font *font, float size)
{
   font->size = size;
   /* SDL_ttf's point size at its default 72 dpi is the em size in pixels */
   font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, size);
   stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->line_gap);
}

extern "C" bool TTF_Init(void) { return true; }
extern "C" void TTF_Quit(void) {}

extern "C" TTF_Font *TTF_OpenFontIO(SDL_IOStream *io, bool closeio, float ptsize)
{
   if (!io)
      return (TTF_Font *)(set_error("TTF_OpenFontIO: no stream"), nullptr);
   TTF_Font *font = new TTF_Font{};
   if (!stbtt_InitFont(&font->info, io->data, stbtt_GetFontOffsetForIndex(io->data, 0))) {
      delete font;
      if (closeio)
         delete io;
      return (TTF_Font *)(set_error("TTF_OpenFontIO: not a font"), nullptr);
   }
   font->io = closeio ? io : nullptr;
   font_set_size(font, ptsize);
   return font;
}

extern "C" void TTF_CloseFont(TTF_Font *font)
{
   if (!font)
      return;
   delete font->io;
   delete font;
}

extern "C" bool TTF_SetFontSize(TTF_Font *font, float ptsize)
{
   if (!font || ptsize <= 0)
      return set_error("TTF_SetFontSize: bad size");
   font_set_size(font, ptsize);
   return true;
}
extern "C" float TTF_GetFontSize(TTF_Font *font) { return font ? font->size : 0.0f; }
extern "C" int TTF_GetFontLineSkip(const TTF_Font *font)
{
   return font ? (int)std::ceil((font->ascent - font->descent + font->line_gap) * font->scale) : 0;
}
extern "C" void TTF_SetFontStyle(TTF_Font *, TTF_FontStyleFlags) {}

/* UTF-8 to code points (invalid bytes become U+FFFD) */
static std::vector<uint32_t> decode_utf8(const char *text, size_t length)
{
   std::vector<uint32_t> out;
   const unsigned char *p = (const unsigned char *)text, *end = p + length;
   while (p < end) {
      uint32_t c = *p++;
      int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
      if (c >= 0x80 && more == 0)
         c = 0xfffd;
      else if (more) {
         c &= 0x3f >> more;
         while (more-- && p < end && (*p & 0xc0) == 0x80)
            c = (c << 6) | (*p++ & 0x3f);
      }
      out.push_back(c);
   }
   return out;
}

static int advance_of(const TTF_Font *font, uint32_t c, uint32_t next)
{
   int advance, bearing;
   stbtt_GetCodepointHMetrics(&font->info, (int)c, &advance, &bearing);
   float a = advance * font->scale;
   if (next)
      a += stbtt_GetCodepointKernAdvance(&font->info, (int)c, (int)next) * font->scale;
   return (int)std::lround(a);
}

/* Lines broken at newlines and, with wrap_width > 0, at the last space that fits (SDL_ttf's rule) */
static std::vector<std::vector<uint32_t>> wrap_lines(const TTF_Font *font, const std::vector<uint32_t> &text,
                                                     int wrap_width)
{
   std::vector<std::vector<uint32_t>> lines(1);
   int width = 0;
   size_t last_space = SIZE_MAX;
   for (uint32_t c : text) {
      if (c == '\n') {
         lines.emplace_back();
         width = 0;
         last_space = SIZE_MAX;
         continue;
      }
      auto &line = lines.back();
      const int a = advance_of(font, c, 0);
      if (wrap_width > 0 && width + a > wrap_width && !line.empty()) {
         if (last_space != SIZE_MAX) {
            std::vector<uint32_t> rest(line.begin() + last_space + 1, line.end());
            line.resize(last_space);
            lines.push_back(rest);
         } else
            lines.emplace_back();
         auto &now = lines.back();
         width = 0;
         for (size_t i = 0; i < now.size(); i++)
            width += advance_of(font, now[i], 0);
         last_space = SIZE_MAX;
      }
      auto &current = lines.back();
      if (c == ' ')
         last_space = current.size();
      current.push_back(c);
      width += a;
   }
   return lines;
}

/* White-on-colour text, each pixel the background blended toward the foreground by the glyph's
 * coverage, opaque (Gopher64 draws it with blending off), in SDL_PIXELFORMAT_ARGB8888 */
extern "C" SDL_Surface *TTF_RenderText_LCD_Wrapped(TTF_Font *font, const char *text, size_t length, SDL_Color fg,
                                                   SDL_Color bg, int wrap_width)
{
   if (!font || !text)
      return (SDL_Surface *)(set_error("TTF_RenderText_LCD_Wrapped: no font or text"), nullptr);
   if (length == 0)
      length = strlen(text);
   const auto lines = wrap_lines(font, decode_utf8(text, length), wrap_width);
   const int line_skip = std::max(1, TTF_GetFontLineSkip(font));
   const int baseline = (int)std::ceil(font->ascent * font->scale);
   int width = 1;
   for (const auto &line : lines) {
      int w = 0;
      for (size_t i = 0; i < line.size(); i++)
         w += advance_of(font, line[i], i + 1 < line.size() ? line[i + 1] : 0);
      width = std::max(width, w);
   }
   const int height = line_skip * (int)lines.size();
   std::vector<uint8_t> coverage((size_t)width * height, 0);
   for (size_t l = 0; l < lines.size(); l++) {
      int x = 0;
      const auto &line = lines[l];
      for (size_t i = 0; i < line.size(); i++) {
         int x0, y0, x1, y1;
         stbtt_GetCodepointBitmapBox(&font->info, (int)line[i], font->scale, font->scale, &x0, &y0, &x1, &y1);
         const int gx = x + x0, gy = (int)l * line_skip + baseline + y0, gw = x1 - x0, gh = y1 - y0;
         if (gw > 0 && gh > 0) {
            std::vector<uint8_t> glyph((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&font->info, glyph.data(), gw, gh, gw, font->scale, font->scale, (int)line[i]);
            for (int y = 0; y < gh; y++)
               for (int xx = 0; xx < gw; xx++) {
                  const int px = gx + xx, py = gy + y;
                  if (px < 0 || py < 0 || px >= width || py >= height)
                     continue;
                  uint8_t &c = coverage[(size_t)py * width + px];
                  c = std::max(c, glyph[(size_t)y * gw + xx]);
               }
         }
         x += advance_of(font, line[i], i + 1 < line.size() ? line[i + 1] : 0);
      }
   }
   SDL_Surface *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
   uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * 4);
   if (!surface || !pixels) {
      free(surface);
      free(pixels);
      return (SDL_Surface *)(set_error("TTF_RenderText_LCD_Wrapped: out of memory"), nullptr);
   }
   for (size_t i = 0; i < coverage.size(); i++) {
      const unsigned a = coverage[i];
      const unsigned r = (fg.r * a + bg.r * (255 - a)) / 255, g = (fg.g * a + bg.g * (255 - a)) / 255,
                     b = (fg.b * a + bg.b * (255 - a)) / 255;
      pixels[i] = 0xff000000u | (r << 16) | (g << 8) | b;
   }
   surface->format = SDL_PIXELFORMAT_ARGB8888;
   surface->w = width;
   surface->h = height;
   surface->pitch = width * 4;
   surface->pixels = pixels;
   surface->refcount = 1;
   return surface;
}

/* The text engine draws through the SDL renderer: the desktop input-profile editor only */
extern "C" TTF_TextEngine *TTF_CreateRendererTextEngine(SDL_Renderer *)
{
   unsupported("TTF_CreateRendererTextEngine");
   return nullptr;
}
extern "C" void TTF_DestroyRendererTextEngine(TTF_TextEngine *) {}
extern "C" TTF_Text *TTF_CreateText(TTF_TextEngine *, TTF_Font *, const char *, size_t)
{
   unsupported("TTF_CreateText");
   return nullptr;
}
extern "C" void TTF_DestroyText(TTF_Text *) {}
extern "C" bool TTF_DrawRendererText(TTF_Text *, float, float) { return false; }
extern "C" bool TTF_GetTextSize(TTF_Text *, int *w, int *h)
{
   if (w)
      *w = 0;
   if (h)
      *h = 0;
   return false;
}
extern "C" bool TTF_SetTextString(TTF_Text *, const char *, size_t) { return false; }
