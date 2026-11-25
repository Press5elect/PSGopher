/*
 * PSGopher - the three VideoOut functions Crux2's RADV asks the title for (variable refresh and HDR10:
 * Crux2's ps5/patches/mesa-27/0017 and 0020), looked up while the title runs.
 *
 * Not every firmware's libSceVideoOut exports them, and a title that imports a function the console
 * lacks does not start at all: so the title does not import them. The driver calls weak hooks; these
 * answer them after finding each function by its name, then its NID, in the process's modules, and -1
 * when the console has none (the driver then refuses variable refresh, or HDR).
 *
 * The lookup is Crux2's PS5SystemModules::FindFunction (ps5/frontend/ps5_keyboard.cpp), as PSWin found
 * it (Crux2 by Press5elect, GPL-3.0-or-later; additional term, GPL-3.0 section 7(b): keep the
 * attribution "Crux2 by Press5elect"), cut to what these three need.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <array>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

extern "C" {
#include "platform.h"
int sceKernelDlsym(int32_t handle, const char *symbol, void **address);
int sceKernelGetModuleList(int32_t *handles, size_t capacity, size_t *count);
int sceKernelGetModuleInfo(int32_t handle, void *info);
}

namespace
{
// The module list's entry: the size of the structure first, the module's name at 8.
constexpr size_t MODULE_INFO_SIZE = 0x160;
constexpr size_t MODULE_NAME_OFFSET = 8;
constexpr size_t MODULE_NAME_MAX = 256;

bool name_matches(const char *name, const char *wanted)
{
   const size_t n = std::strlen(wanted);
   return std::strncmp(name, wanted, n) == 0 && (name[n] == '\0' || name[n] == '.');
}

std::vector<int32_t> module_list()
{
   std::array<int32_t, 256> handles;
   size_t count = 0;
   if (sceKernelGetModuleList(handles.data(), handles.size(), &count) != 0)
      return {};
   return std::vector<int32_t>(handles.begin(), handles.begin() + static_cast<std::ptrdiff_t>(count < handles.size() ? count : handles.size()));
}

std::string module_name(int32_t handle)
{
   alignas(8) uint8_t info[MODULE_INFO_SIZE] = {};
   const uint64_t size = sizeof(info);
   std::memcpy(info, &size, sizeof(size));
   if (sceKernelGetModuleInfo(handle, info) != 0)
      return {};
   char name[MODULE_NAME_MAX + 1] = {};
   std::memcpy(name, info + MODULE_NAME_OFFSET, MODULE_NAME_MAX);
   return name;
}

void *find_in(int32_t module, const char *function, const char *nid)
{
   if (module < 0)
      return nullptr;
   void *address = nullptr;
   if (sceKernelDlsym(module, function, &address) == 0 && address)
      return address;
   address = nullptr;
   if (nid && sceKernelDlsym(module, nid, &address) == 0 && address)
      return address;
   return dlsym(reinterpret_cast<void *>(static_cast<intptr_t>(module)), function);
}

/* The named module first, then every module of the process, then the system modules' handles (not in
 * the list a title sees: the small numbers in load order and the 0x2000 range). */
void *find_function(const char *module, const char *function, const char *nid)
{
   const std::vector<int32_t> modules = module_list();
   int32_t named = -1;
   for (const int32_t handle : modules)
      if (name_matches(module_name(handle).c_str(), module)) {
         named = handle;
         break;
      }
   if (void *address = find_in(named, function, nid))
      return address;
   for (const int32_t handle : modules)
      if (handle != named)
         if (void *address = find_in(handle, function, nid))
            return address;
   for (int32_t handle = 0; handle < 0x2040; handle = (handle == 511) ? 0x2000 : handle + 1) {
      bool listed = handle == named;
      for (const int32_t m : modules)
         listed |= (m == handle);
      if (!listed)
         if (void *address = find_in(handle, function, nid))
            return address;
   }
   return nullptr;
}
} // namespace

/* Variable refresh (0017): sceVideoOutVrrUnpegFromFixedRate */
extern "C" int ps5_title_videoout_vrr_unpeg(int32_t handle)
{
   using UnpegFn = int (*)(int32_t);
   static const UnpegFn unpeg = [] {
      const auto found = reinterpret_cast<UnpegFn>(find_function("libSceVideoOut", "sceVideoOutVrrUnpegFromFixedRate", "T4ucGB8CsnM"));
      say("display: sceVideoOutVrrUnpegFromFixedRate %s", found ? "found" : "not on this console: no variable refresh");
      return found;
   }();
   return unpeg ? unpeg(handle) : -1;
}

/* HDR10 (0020): sceVideoOutSubmitChangeBufferAttribute2 retypes the framebuffers */
extern "C" int ps5_title_videoout_change_attribute(int32_t handle, int32_t set, const void *attribute)
{
   using ChangeFn = int (*)(int32_t, int32_t, const void *, const void *);
   static const ChangeFn change = [] {
      const auto found =
         reinterpret_cast<ChangeFn>(find_function("libSceVideoOut", "sceVideoOutSubmitChangeBufferAttribute2", "HuViW4HnrOw"));
      say("display: sceVideoOutSubmitChangeBufferAttribute2 %s", found ? "found" : "not on this console: no HDR output");
      return found;
   }();
   return change ? change(handle, set, attribute, nullptr) : -1;
}

/* HDR10 (0020): sceVideoOutGetOutputStatus says whether the output is SDR or HDR */
extern "C" int ps5_title_videoout_output_status(int32_t handle, void *status)
{
   using StatusFn = int (*)(int32_t, void *);
   static const StatusFn get = [] {
      const auto found = reinterpret_cast<StatusFn>(find_function("libSceVideoOut", "sceVideoOutGetOutputStatus", "utPrVdxio-8"));
      say("display: sceVideoOutGetOutputStatus %s", found ? "found" : "not on this console: the output's range unknown");
      return found;
   }();
   return get ? get(handle, status) : -1;
}
