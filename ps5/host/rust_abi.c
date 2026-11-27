/*
 * PSGopher - FreeBSD 12's structures for Rust, over the console's FreeBSD 11 ones.
 *
 * Rust's standard library (and the libc crate under every Rust crate) is built for FreeBSD's
 * 12 ABI: 64-bit inode and device numbers in struct stat and struct dirent, and struct kevent
 * with four extension words. Its libc cannot be built for 11 any more (std itself no longer
 * compiles against it). The console's libc is FreeBSD 11's (the SDK's sys/stat.h, sys/dirent.h,
 * sys/event.h). So each field Rust read was at the wrong place: the first console runs had every
 * game stop at Gopher64's create_dir_all ("File exists": a folder's stat did not say "folder"),
 * and Tokio's whole runtime refused ("Operation not supported": mio's kevent got 32-byte events
 * for 64-byte ones). ps5/tools/build-core.sh renames the Rust core's calls of these four
 * functions to the ones here (llvm-objcopy --redefine-sym; no C object in the core calls them),
 * which convert, as FreeBSD's own compatibility functions do the other way.
 *
 * The 11 structures are checked against the SDK's at compile time on the console; the 12 ones
 * against the Rust libc's own offsets by ps5/tests/test_rust_abi.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- FreeBSD 11, the console's ---- */
struct psg_timespec {
   int64_t tv_sec;
   long tv_nsec;
};
struct psg_stat11 {
   uint32_t st_dev;
   uint32_t st_ino;
   uint16_t st_mode;
   uint16_t st_nlink;
   uint32_t st_uid;
   uint32_t st_gid;
   uint32_t st_rdev;
   struct psg_timespec st_atim, st_mtim, st_ctim;
   int64_t st_size;
   int64_t st_blocks;
   int32_t st_blksize;
   uint32_t st_flags;
   uint32_t st_gen;
   int32_t st_lspare;
   struct psg_timespec st_birthtim;
};
struct psg_dirent11 {
   uint32_t d_fileno;
   uint16_t d_reclen;
   uint8_t d_type;
   uint8_t d_namlen;
   char d_name[256];
};
struct psg_kevent11 {
   uintptr_t ident;
   short filter;
   unsigned short flags;
   unsigned int fflags;
   intptr_t data;
   void *udata;
};

/* ---- FreeBSD 12, Rust's libc ---- */
struct psg_stat12 {
   uint64_t st_dev;
   uint64_t st_ino;
   uint64_t st_nlink;
   uint16_t st_mode;
   int16_t st_padding0;
   uint32_t st_uid;
   uint32_t st_gid;
   int32_t st_padding1;
   uint64_t st_rdev;
   struct psg_timespec st_atim, st_mtim, st_ctim, st_birthtim;
   int64_t st_size;
   int64_t st_blocks;
   int32_t st_blksize;
   uint32_t st_flags;
   uint64_t st_gen;
   uint64_t st_spare[10];
};
struct psg_dirent12 {
   uint64_t d_fileno;
   int64_t d_off;
   uint16_t d_reclen;
   uint8_t d_type;
   uint8_t d_pad0;
   uint16_t d_namlen;
   uint16_t d_pad1;
   char d_name[256];
};
struct psg_kevent12 {
   uintptr_t ident;
   short filter;
   unsigned short flags;
   unsigned int fflags;
   int64_t data;
   void *udata;
   uint64_t ext[4];
};
_Static_assert(sizeof(struct psg_stat12) == 224, "FreeBSD 12 struct stat");
_Static_assert(sizeof(struct psg_dirent12) == 280, "FreeBSD 12 struct dirent");
_Static_assert(sizeof(struct psg_kevent12) == 64, "FreeBSD 12 struct kevent");

#if defined(__PROSPERO__)
#include <sys/types.h>
#include <dirent.h>
#include <sys/event.h>
#include <sys/stat.h>
#define SAME(a, b, f) _Static_assert(offsetof(struct a, f) == offsetof(struct b, f) && \
                                        sizeof(((struct a *)0)->f) == sizeof(((struct b *)0)->f), #a "." #f)
SAME(psg_stat11, stat, st_dev);
SAME(psg_stat11, stat, st_ino);
SAME(psg_stat11, stat, st_mode);
SAME(psg_stat11, stat, st_nlink);
SAME(psg_stat11, stat, st_uid);
SAME(psg_stat11, stat, st_gid);
SAME(psg_stat11, stat, st_rdev);
SAME(psg_stat11, stat, st_atim);
SAME(psg_stat11, stat, st_mtim);
SAME(psg_stat11, stat, st_ctim);
SAME(psg_stat11, stat, st_size);
SAME(psg_stat11, stat, st_blocks);
SAME(psg_stat11, stat, st_blksize);
SAME(psg_stat11, stat, st_flags);
SAME(psg_stat11, stat, st_gen);
SAME(psg_stat11, stat, st_birthtim);
_Static_assert(sizeof(struct psg_stat11) <= sizeof(struct stat), "struct stat");
SAME(psg_dirent11, dirent, d_fileno);
SAME(psg_dirent11, dirent, d_reclen);
SAME(psg_dirent11, dirent, d_type);
SAME(psg_dirent11, dirent, d_namlen);
SAME(psg_dirent11, dirent, d_name);
SAME(psg_kevent11, kevent, ident);
SAME(psg_kevent11, kevent, filter);
SAME(psg_kevent11, kevent, flags);
SAME(psg_kevent11, kevent, fflags);
SAME(psg_kevent11, kevent, data);
SAME(psg_kevent11, kevent, udata);
_Static_assert(sizeof(struct psg_kevent11) == sizeof(struct kevent), "struct kevent");
/* the console's functions, by their 11 structures */
#define STAT11(path, st) stat((path), (struct stat *)(st))
#define FSTAT11(fd, st) fstat((fd), (struct stat *)(st))
#define READDIR11(dir) ((struct psg_dirent11 *)readdir((DIR *)(dir)))
#define KEVENT11(kq, ch, nch, ev, nev, t) \
   kevent((kq), (const struct kevent *)(ch), (nch), (struct kevent *)(ev), (nev), (t))
#else
/* the host test gives its own */
int test_stat11(const char *path, struct psg_stat11 *st);
int test_fstat11(int fd, struct psg_stat11 *st);
struct psg_dirent11 *test_readdir11(void *dir);
int test_kevent11(int kq, const struct psg_kevent11 *ch, int nch, struct psg_kevent11 *ev, int nev, const void *t);
#define STAT11 test_stat11
#define FSTAT11 test_fstat11
#define READDIR11 test_readdir11
#define KEVENT11 test_kevent11
#endif

/* reserved: a stat that differs between the two is left for the reader to see as zero */
static void
stat12(struct psg_stat12 *out, const struct psg_stat11 *in)
{
   memset(out, 0, sizeof(*out));
   out->st_dev = in->st_dev;
   out->st_ino = in->st_ino;
   out->st_nlink = in->st_nlink;
   out->st_mode = in->st_mode;
   out->st_uid = in->st_uid;
   out->st_gid = in->st_gid;
   out->st_rdev = in->st_rdev;
   out->st_atim = in->st_atim;
   out->st_mtim = in->st_mtim;
   out->st_ctim = in->st_ctim;
   out->st_birthtim = in->st_birthtim;
   out->st_size = in->st_size;
   out->st_blocks = in->st_blocks;
   out->st_blksize = in->st_blksize;
   out->st_flags = in->st_flags;
   out->st_gen = in->st_gen;
}

int
psgopher_fb12_stat(const char *path, struct psg_stat12 *st)
{
   struct psg_stat11 old;
   memset(&old, 0, sizeof(old));
   const int r = STAT11(path, &old);
   if (r == 0)
      stat12(st, &old);
   return r;
}

int
psgopher_fb12_fstat(int fd, struct psg_stat12 *st)
{
   struct psg_stat11 old;
   memset(&old, 0, sizeof(old));
   const int r = FSTAT11(fd, &old);
   if (r == 0)
      stat12(st, &old);
   return r;
}

/* one entry per thread, valid until that thread's next readdir (what readdir promises per
 * directory; Rust's read_dir copies each entry's fields and name before it asks again) */
static __thread struct psg_dirent12 dirent_now;

struct psg_dirent12 *
psgopher_fb12_readdir(void *dir)
{
   const struct psg_dirent11 *old = READDIR11(dir);
   if (!old)
      return NULL;
   memset(&dirent_now, 0, offsetof(struct psg_dirent12, d_name));
   dirent_now.d_fileno = old->d_fileno;
   dirent_now.d_reclen = sizeof(struct psg_dirent12);
   dirent_now.d_type = old->d_type;
   size_t n = old->d_namlen;
   if (n > 255)
      n = 255;
   memcpy(dirent_now.d_name, old->d_name, n);
   dirent_now.d_name[n] = 0;
   dirent_now.d_namlen = (uint16_t)n;
   return &dirent_now;
}

int
psgopher_fb12_kevent(int kq, const struct psg_kevent12 *changes, int nchanges, struct psg_kevent12 *events,
                     int nevents, const void *timeout)
{
   enum { ON_STACK = 64 };
   struct psg_kevent11 change_stack[ON_STACK], event_stack[ON_STACK];
   struct psg_kevent11 *ch = change_stack, *ev = event_stack;
   if (nchanges < 0 || nevents < 0) {
      errno = EINVAL;
      return -1;
   }
   if (nchanges > ON_STACK && !(ch = malloc(sizeof(*ch) * (size_t)nchanges))) {
      errno = ENOMEM;
      return -1;
   }
   if (nevents > ON_STACK && !(ev = malloc(sizeof(*ev) * (size_t)nevents))) {
      if (ch != change_stack)
         free(ch);
      errno = ENOMEM;
      return -1;
   }
   for (int i = 0; i < nchanges; i++) {
      ch[i].ident = changes[i].ident;
      ch[i].filter = changes[i].filter;
      ch[i].flags = changes[i].flags;
      ch[i].fflags = changes[i].fflags;
      ch[i].data = (intptr_t)changes[i].data;
      ch[i].udata = changes[i].udata;
   }
   const int n = KEVENT11(kq, nchanges ? ch : NULL, nchanges, nevents ? ev : NULL, nevents, timeout);
   const int saved = errno;
   for (int i = 0; i < n && i < nevents; i++) {
      events[i].ident = ev[i].ident;
      events[i].filter = ev[i].filter;
      events[i].flags = ev[i].flags;
      events[i].fflags = ev[i].fflags;
      events[i].data = ev[i].data;
      events[i].udata = ev[i].udata;
      memset(events[i].ext, 0, sizeof(events[i].ext));
   }
   if (ch != change_stack)
      free(ch);
   if (ev != event_stack)
      free(ev);
   errno = saved;
   return n;
}
