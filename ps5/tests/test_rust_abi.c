/*
 * PSGopher - host test of ps5/host/rust_abi.c: its FreeBSD 12 structures against the offsets and
 * sizes of Rust's libc itself (ps5/tests/rust_abi_offsets, built for x86_64-unknown-freebsd), and
 * the four converters against FreeBSD 11 answers given here.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define main rust_abi_unused_main
#include "../host/rust_abi.c"
#undef main

#include <stdio.h>

static int failures;
#define CHECK(x)                                                              \
   do {                                                                       \
      if (!(x)) {                                                             \
         fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);       \
         failures++;                                                          \
      }                                                                       \
   } while (0)

size_t psg_rust_abi(char *out, size_t room);

static long rust(const char *text, const char *name)
{
   char key[64];
   snprintf(key, sizeof(key), "\n%s ", name);
   const char *at = strstr(text, key);
   return at ? strtol(at + strlen(key), NULL, 10) : -1;
}

/* ---- the console's answers, FreeBSD 11 ---- */
int test_stat11(const char *path, struct psg_stat11 *st)
{
   if (strcmp(path, "/data/folder") != 0) {
      errno = ENOENT;
      return -1;
   }
   st->st_dev = 0x11223344u;
   st->st_ino = 0xdeadbeefu;
   st->st_mode = 0040755; /* a folder */
   st->st_nlink = 3;
   st->st_uid = 1000;
   st->st_gid = 1001;
   st->st_rdev = 7;
   st->st_atim = (struct psg_timespec){ 100, 1 };
   st->st_mtim = (struct psg_timespec){ 200, 2 };
   st->st_ctim = (struct psg_timespec){ 300, 3 };
   st->st_birthtim = (struct psg_timespec){ 50, 4 };
   st->st_size = 0x123456789ll;
   st->st_blocks = 99;
   st->st_blksize = 65536;
   st->st_flags = 0x20;
   st->st_gen = 9;
   return 0;
}
int test_fstat11(int fd, struct psg_stat11 *st)
{
   return fd == 5 ? test_stat11("/data/folder", st) : (errno = EBADF, -1);
}
static struct psg_dirent11 entries[2];
static int entry_at;
struct psg_dirent11 *test_readdir11(void *dir)
{
   (void)dir;
   return entry_at < 2 ? &entries[entry_at++] : NULL;
}
static struct psg_kevent11 seen_changes[100];
static int seen_nchanges;
int test_kevent11(int kq, const struct psg_kevent11 *ch, int nch, struct psg_kevent11 *ev, int nev, const void *t)
{
   (void)kq;
   (void)t;
   seen_nchanges = nch;
   for (int i = 0; i < nch && i < 100; i++)
      seen_changes[i] = ch[i];
   int n = 0;
   for (; n < nev && n < 2; n++)
      ev[n] = (struct psg_kevent11){ .ident = 40 + n, .filter = -11 /* EVFILT_USER */, .flags = 0x20,
                                     .fflags = 0x1000000, .data = -5 - n, .udata = (void *)(uintptr_t)(0x1000 + n) };
   return n;
}

int main(void)
{
   /* the 12 structures, field by field, against Rust's libc */
   static char table[8192];
   table[0] = '\n';
   const size_t n = psg_rust_abi(table + 1, sizeof(table) - 2);
   table[n + 1] = 0;
#define FIELD(rs, cs, rf, cf) CHECK(rust(table, #rs "." #rf) == (long)offsetof(struct cs, cf))
   CHECK(rust(table, "stat.size") == (long)sizeof(struct psg_stat12));
   FIELD(stat, psg_stat12, st_dev, st_dev);
   FIELD(stat, psg_stat12, st_ino, st_ino);
   FIELD(stat, psg_stat12, st_nlink, st_nlink);
   FIELD(stat, psg_stat12, st_mode, st_mode);
   FIELD(stat, psg_stat12, st_uid, st_uid);
   FIELD(stat, psg_stat12, st_gid, st_gid);
   FIELD(stat, psg_stat12, st_rdev, st_rdev);
   FIELD(stat, psg_stat12, st_atime, st_atim.tv_sec);
   FIELD(stat, psg_stat12, st_atime_nsec, st_atim.tv_nsec);
   FIELD(stat, psg_stat12, st_mtime, st_mtim.tv_sec);
   FIELD(stat, psg_stat12, st_mtime_nsec, st_mtim.tv_nsec);
   FIELD(stat, psg_stat12, st_ctime, st_ctim.tv_sec);
   FIELD(stat, psg_stat12, st_ctime_nsec, st_ctim.tv_nsec);
   FIELD(stat, psg_stat12, st_birthtime, st_birthtim.tv_sec);
   FIELD(stat, psg_stat12, st_birthtime_nsec, st_birthtim.tv_nsec);
   FIELD(stat, psg_stat12, st_size, st_size);
   FIELD(stat, psg_stat12, st_blocks, st_blocks);
   FIELD(stat, psg_stat12, st_blksize, st_blksize);
   FIELD(stat, psg_stat12, st_flags, st_flags);
   FIELD(stat, psg_stat12, st_gen, st_gen);
   CHECK(rust(table, "dirent.size") == (long)sizeof(struct psg_dirent12));
   FIELD(dirent, psg_dirent12, d_fileno, d_fileno);
   FIELD(dirent, psg_dirent12, d_off, d_off);
   FIELD(dirent, psg_dirent12, d_reclen, d_reclen);
   FIELD(dirent, psg_dirent12, d_type, d_type);
   FIELD(dirent, psg_dirent12, d_namlen, d_namlen);
   FIELD(dirent, psg_dirent12, d_name, d_name);
   CHECK(rust(table, "kevent.size") == (long)sizeof(struct psg_kevent12));
   FIELD(kevent, psg_kevent12, ident, ident);
   FIELD(kevent, psg_kevent12, filter, filter);
   FIELD(kevent, psg_kevent12, flags, flags);
   FIELD(kevent, psg_kevent12, fflags, fflags);
   FIELD(kevent, psg_kevent12, data, data);
   FIELD(kevent, psg_kevent12, udata, udata);
   FIELD(kevent, psg_kevent12, ext, ext);

   /* stat and fstat */
   struct psg_stat12 st;
   memset(&st, 0xab, sizeof(st));
   CHECK(psgopher_fb12_stat("/data/folder", &st) == 0);
   CHECK(st.st_dev == 0x11223344u && st.st_ino == 0xdeadbeefu && st.st_nlink == 3 && st.st_mode == 0040755);
   CHECK((st.st_mode & 0170000) == 0040000); /* is_dir() */
   CHECK(st.st_uid == 1000 && st.st_gid == 1001 && st.st_rdev == 7);
   CHECK(st.st_atim.tv_sec == 100 && st.st_mtim.tv_sec == 200 && st.st_mtim.tv_nsec == 2 && st.st_ctim.tv_sec == 300);
   CHECK(st.st_birthtim.tv_sec == 50 && st.st_size == 0x123456789ll && st.st_blocks == 99 && st.st_blksize == 65536);
   CHECK(st.st_flags == 0x20 && st.st_gen == 9 && st.st_spare[9] == 0 && st.st_padding0 == 0);
   errno = 0;
   CHECK(psgopher_fb12_stat("/nowhere", &st) == -1 && errno == ENOENT);
   memset(&st, 0, sizeof(st));
   CHECK(psgopher_fb12_fstat(5, &st) == 0 && st.st_size == 0x123456789ll);
   CHECK(psgopher_fb12_fstat(6, &st) == -1 && errno == EBADF);

   /* readdir: a short name and a 255-character one */
   entries[0] = (struct psg_dirent11){ .d_fileno = 77, .d_reclen = 12, .d_type = 4, .d_namlen = 3 };
   memcpy(entries[0].d_name, "rom", 4);
   entries[1] = (struct psg_dirent11){ .d_fileno = 78, .d_reclen = 264, .d_type = 8, .d_namlen = 255 };
   memset(entries[1].d_name, 'x', 255);
   struct psg_dirent12 *e = psgopher_fb12_readdir(NULL);
   CHECK(e && e->d_fileno == 77 && e->d_type == 4 && e->d_namlen == 3 && strcmp(e->d_name, "rom") == 0);
   e = psgopher_fb12_readdir(NULL);
   CHECK(e && e->d_fileno == 78 && e->d_type == 8 && e->d_namlen == 255 && strlen(e->d_name) == 255);
   CHECK(psgopher_fb12_readdir(NULL) == NULL);

   /* kevent: changes in, events out, beyond the on-stack 64 too */
   struct psg_kevent12 changes[100], events[3];
   for (int i = 0; i < 100; i++)
      changes[i] = (struct psg_kevent12){ .ident = (uintptr_t)i, .filter = -11, .flags = 0x11, .fflags = (unsigned)i * 3,
                                          .data = i * 7, .udata = (void *)(uintptr_t)(0x2000 + i), .ext = { 1, 2, 3, 4 } };
   memset(events, 0xcd, sizeof(events));
   CHECK(psgopher_fb12_kevent(3, changes, 100, events, 3, NULL) == 2);
   CHECK(seen_nchanges == 100);
   CHECK(seen_changes[99].ident == 99 && seen_changes[99].fflags == 297 && seen_changes[99].data == 693 &&
         seen_changes[99].udata == (void *)(uintptr_t)(0x2000 + 99) && seen_changes[0].filter == -11 && seen_changes[0].flags == 0x11);
   CHECK(events[0].ident == 40 && events[0].filter == -11 && events[0].flags == 0x20 && events[0].fflags == 0x1000000);
   CHECK(events[1].data == -6 && events[1].udata == (void *)(uintptr_t)0x1001 && events[1].ext[0] == 0 && events[1].ext[3] == 0);
   CHECK(psgopher_fb12_kevent(3, NULL, 0, events, 1, NULL) == 1 && seen_nchanges == 0);

   if (failures) {
      fprintf(stderr, "test_rust_abi: %d check(s) failed\n%s", failures, table);
      return 1;
   }
   printf("test_rust_abi: all checks passed (FreeBSD 12 stat %zu, dirent %zu, kevent %zu bytes, as Rust's libc)\n",
          sizeof(struct psg_stat12), sizeof(struct psg_dirent12), sizeof(struct psg_kevent12));
   return 0;
}
