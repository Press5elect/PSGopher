/*
 * PSGopher - the crash report: Crux2's (ps5/frontend/ps5_crash.cpp, Crux2 by Press5elect), in C on
 * PSGopher's boot log.
 *
 * A title that faults is closed by the console with an error code and nothing else. This handler
 * first writes what is needed into the boot log (the signal, the faulting address, the registers,
 * and the return addresses up the stack, by frame pointer: the title and the Rust core are built
 * with them), then hands the signal back to the console, which closes the title as it would have.
 * Addresses are also given as offsets in the program, which is linked at 0, so that each is looked
 * up in the link's output as it is:
 *
 *     llvm-addr2line -f -C -e build/ps5/link/llvm-pie.elf 0x<offset>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
 */
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(__PROSPERO__)
/* refuses to build against an SDK whose ucontext_t is not the console's layout */
#include <ps5platform/context.h>
#endif

int platform_log_fd(void);
int sceKernelDebugOutText(int channel, const char *text);

/* the program's first byte (the link puts __dso_handle there, at 0) */
extern char __dso_handle[] __attribute__((weak, visibility("hidden")));

static volatile sig_atomic_t in_crash;

static const char *
signal_name(int signal)
{
   switch (signal) {
   case SIGSEGV: return "SIGSEGV (invalid memory access)";
   case SIGBUS: return "SIGBUS (bus error)";
   case SIGILL: return "SIGILL (illegal instruction)";
   case SIGFPE: return "SIGFPE (arithmetic error)";
   case SIGABRT: return "SIGABRT (abort)";
   case SIGTRAP: return "SIGTRAP (trap)";
   case SIGSYS: return "SIGSYS (bad system call)";
   default: return "signal";
   }
}

/* formatting on the stack and write(): nothing here allocates or takes a lock */
static void line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void
line(const char *format, ...)
{
   char text[256];
   va_list args;
   va_start(args, format);
   int length = vsnprintf(text, sizeof(text) - 1, format, args);
   va_end(args);
   if (length < 0)
      return;
   if (length > (int)sizeof(text) - 2)
      length = (int)sizeof(text) - 2;
   text[length++] = '\n';
   const int fd = platform_log_fd();
   if (fd >= 0)
      (void)!write(fd, text, (size_t)length);
   text[length] = 0;
#if defined(__PROSPERO__)
   (void)sceKernelDebugOutText(0, text);
#endif
}

static void
address(const char *what, uintptr_t at)
{
   const uintptr_t base = (uintptr_t)&__dso_handle[0];
   if (base != 0 && at >= base && at - base < 0x20000000u)
      line("  %-8s 0x%016llx  (program + 0x%llx)", what, (unsigned long long)at, (unsigned long long)(at - base));
   else
      line("  %-8s 0x%016llx", what, (unsigned long long)at);
}

static void
restore_default(int signal)
{
   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = SIG_DFL;
   (void)sigaction(signal, &sa, NULL);
}

static void
crash_handler(int signal, siginfo_t *info, void *ctx)
{
   /* a second fault (another thread, or this walk on a broken stack): the console takes it */
   if (in_crash) {
      restore_default(signal);
      return;
   }
   in_crash = 1;
   line("==== PSGopher stopped: %s ====", signal_name(signal));
   line("  signal %d, code %d, fault address 0x%016llx", signal, info ? info->si_code : 0,
        (unsigned long long)(info ? (uintptr_t)info->si_addr : 0));
   address("program", (uintptr_t)&__dso_handle[0]);
#if defined(__PROSPERO__) || defined(__FreeBSD__)
   const ucontext_t *context = (const ucontext_t *)ctx;
   if (context) {
      const mcontext_t *mc = &context->uc_mcontext;
      address("rip", (uintptr_t)mc->mc_rip);
      line("  rsp 0x%016llx  rbp 0x%016llx  err 0x%llx", (unsigned long long)mc->mc_rsp,
           (unsigned long long)mc->mc_rbp, (unsigned long long)mc->mc_err);
      line("  rax 0x%016llx  rbx 0x%016llx  rcx 0x%016llx  rdx 0x%016llx", (unsigned long long)mc->mc_rax,
           (unsigned long long)mc->mc_rbx, (unsigned long long)mc->mc_rcx, (unsigned long long)mc->mc_rdx);
      line("  rsi 0x%016llx  rdi 0x%016llx  r8  0x%016llx  r9  0x%016llx", (unsigned long long)mc->mc_rsi,
           (unsigned long long)mc->mc_rdi, (unsigned long long)mc->mc_r8, (unsigned long long)mc->mc_r9);
      line("  r10 0x%016llx  r11 0x%016llx  r12 0x%016llx  r13 0x%016llx", (unsigned long long)mc->mc_r10,
           (unsigned long long)mc->mc_r11, (unsigned long long)mc->mc_r12, (unsigned long long)mc->mc_r13);
      line("  r14 0x%016llx  r15 0x%016llx", (unsigned long long)mc->mc_r14, (unsigned long long)mc->mc_r15);
      if (platform_log_fd() >= 0)
         fsync(platform_log_fd());
      /* the return addresses, by frame pointer; the registers are already on disk should one lead
       * nowhere */
      line("  stack:");
      uintptr_t frame = (uintptr_t)mc->mc_rbp;
      const uintptr_t stack = (uintptr_t)mc->mc_rsp;
      for (int depth = 0; depth < 32; depth++) {
         /* a frame is above the stack pointer, within a thread's stack, and aligned */
         if (frame < stack || frame - stack > 0x200000 || (frame & 7) != 0)
            break;
         const uintptr_t *words = (const uintptr_t *)frame;
         const uintptr_t next = words[0], ret = words[1];
         if (ret == 0)
            break;
         char label[16];
         snprintf(label, sizeof(label), "#%d", depth);
         address(label, ret);
         if (next <= frame)
            break;
         frame = next;
      }
   }
#else
   (void)ctx;
#endif
   line("==== end of the report ====");
   if (platform_log_fd() >= 0)
      fsync(platform_log_fd());
   /* back to the console: returning runs the faulting instruction again with the default action */
   restore_default(signal);
}

void
psgopher_crash_install(void)
{
   /* the main thread's own stack for the handler (a stack overflow there is reported too) */
   static char altstack[128 * 1024];
   stack_t ss;
   memset(&ss, 0, sizeof(ss));
   ss.ss_sp = altstack;
   ss.ss_size = sizeof(altstack);
   const int alt = sigaltstack(&ss, NULL);
   static const int fatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS };
   int installed = 0;
   for (unsigned i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++) {
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
      sa.sa_sigaction = crash_handler;
      installed += sigaction(fatal[i], &sa, NULL) == 0;
   }
   char text[160];
   snprintf(text, sizeof(text), "crash report: %d signals%s; look addresses up with llvm-addr2line -f -C -e "
            "build/ps5/link/llvm-pie.elf 0x<offset>", installed, alt == 0 ? "" : " (no alternate stack)");
   line("%s", text);
}
