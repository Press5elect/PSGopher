/*
 * PSGopher - libc names Rust's standard library and its crates import that no console module exports
 * and the platform layer does not provide (PS5_DEPENDENCY_AUDIT.md, "The core's imports").
 *
 * Each is defined as psgopher_<name> and bound to <name> at the link (ps5/tools/link-title.sh,
 * --defsym, kept local), PS5_Vulkan's recipe's way: a title that defined a name a console stub
 * library also defines would export it, and the title converter refuses exports. The names the
 * platform layer has (accept4, gai_strerror, realpath...) are bound to its ps5_ functions instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

void arc4random_buf(void *buffer, size_t bytes); /* bound to the platform's by the link recipe */

/* getrandom(2), as FreeBSD 12 has it, which the getrandom crate (rand's SysRng) calls. Filled from
 * arc4random_buf, which never blocks or fails. */
ssize_t psgopher_getrandom(void *buffer, size_t bytes, unsigned int flags)
{
   (void)flags;
   if (bytes > (size_t)INT32_MAX)
      bytes = (size_t)INT32_MAX;
   arc4random_buf(buffer, bytes);
   return (ssize_t)bytes;
}

struct dl_phdr_info;

/* std's backtraces walk the loaded objects; a title has no loader to ask, so there are none (OGBox
 * does the same): a panic's backtrace prints addresses only */
int psgopher_dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
   (void)callback;
   (void)data;
   return 0;
}

/* Linux's and FreeBSD 13's pthread_setname_np, which std names its threads with: FreeBSD's older
 * pthread_set_name_np is what the console exports */
int psgopher_pthread_setname_np(pthread_t thread, const char *name)
{
   if (!name)
      return EINVAL;
   pthread_set_name_np(thread, name);
   return 0;
}

struct mmsghdr;
struct psgopher_mmsghdr { /* FreeBSD's struct mmsghdr */
   struct msghdr msg_hdr;
   ssize_t msg_len;
};

/* recvmmsg(2), which quinn-udp (netplay's WebRTC, never started: D10) calls: one recvmsg per
 * message, the first with the caller's flags and the rest without waiting */
ssize_t psgopher_recvmmsg(int s, struct mmsghdr *messages, size_t count, int flags, const struct timespec *timeout)
{
   (void)timeout;
   struct psgopher_mmsghdr *m = (struct psgopher_mmsghdr *)messages;
   size_t received = 0;
   for (; received < count; received++) {
      const ssize_t n = recvmsg(s, &m[received].msg_hdr, received ? (flags | MSG_DONTWAIT) : flags);
      if (n < 0) {
         if (received)
            break; /* what arrived so far; the error comes again on the next call */
         return -1;
      }
      m[received].msg_len = n;
   }
   return (ssize_t)received;
}
