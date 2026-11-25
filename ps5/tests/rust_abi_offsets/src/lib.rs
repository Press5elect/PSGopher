//! Offsets and sizes of the FreeBSD 12 structures in Rust's libc, the version Rust's std uses
//! (keep `=0.2.x` in Cargo.toml at the std's: build-core.sh's toolchain, library/Cargo.lock).
#![no_std]

use core::mem::{offset_of, size_of};

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

macro_rules! table {
    ($($t:ident: $($f:ident),*;)*) => {
        /// name=value lines: "stat.size 224", "stat.st_mode 24", ...
        #[unsafe(no_mangle)]
        pub extern "C" fn psg_rust_abi(out: *mut u8, room: usize) -> usize {
            let mut at = 0usize;
            let mut put = |s: &[u8]| {
                for b in s { if at < room { unsafe { *out.add(at) = *b }; at += 1; } }
            };
            let mut num = |v: usize, put: &mut dyn FnMut(&[u8])| {
                let mut digits = [0u8; 20];
                let mut n = v;
                let mut i = digits.len();
                loop { i -= 1; digits[i] = b'0' + (n % 10) as u8; n /= 10; if n == 0 { break; } }
                put(&digits[i..]);
            };
            $(
                put(concat!(stringify!($t), ".size ").as_bytes()); num(size_of::<libc::$t>(), &mut put); put(b"\n");
                $( put(concat!(stringify!($t), ".", stringify!($f), " ").as_bytes()); num(offset_of!(libc::$t, $f), &mut put); put(b"\n"); )*
            )*
            at
        }
    };
}

table! {
    stat: st_dev, st_ino, st_nlink, st_mode, st_uid, st_gid, st_rdev, st_atime, st_atime_nsec, st_mtime,
        st_mtime_nsec, st_ctime, st_ctime_nsec, st_birthtime, st_birthtime_nsec, st_size, st_blocks,
        st_blksize, st_flags, st_gen;
    dirent: d_fileno, d_off, d_reclen, d_type, d_namlen, d_name;
    kevent: ident, filter, flags, fflags, data, udata, ext;
}
