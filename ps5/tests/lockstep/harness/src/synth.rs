// The synthetic test ROM: a tiny program written here (no game, no SDK, nothing downloaded) that
// keeps the VR4300 and the RSP busy with a wide mix of instructions whose results feed back into
// each other, so a wrong result anywhere changes the state that follows.
//
// Boot: the PIF ROM copies ROM 0x40..0x1000 to DMEM and jumps to 0xA4000040, where boot code
// PI-DMAs the image below from ROM 0x1000 to RDRAM 0 and jumps to 0x80000400.
//
// RDRAM image (= ROM 0x1000..):
//   0x0100  interrupt counter, CPU checksum      0x2000  RSP program (IMEM image)
//   0x0180  exception handler (VI, DP)           0x3000  DMEM image (vector inputs)
//   0x0400  main loop                            0x4000  RDP command list (fill, SyncFull)
//                                                0x5000  CPU data (1 KiB, pseudo-random)
// Main loop, forever: a CPU block (ALU, 64-bit, multiply/divide, loads/stores of every width,
// cached and uncached, COP1 single/double arithmetic, compares and branches, likely branches);
// when the RSP is halted, fold its results (DMEM) into the checksum, give it a new seed and
// restart it; every 16 rounds, an RDP list. VI and DP interrupts are taken and acknowledged.
//
// SPDX-License-Identifier: GPL-3.0-or-later
use crate::asm::*;

const IMAGE_LEN: usize = 0x5400;

fn put(image: &mut [u8], at: usize, words: &[u32]) {
    for (k, w) in words.iter().enumerate() {
        image[at + 4 * k..at + 4 * k + 4].copy_from_slice(&w.to_be_bytes());
    }
}

fn boot_code() -> Vec<u32> {
    let mut a = Asm::new(0xA400_0040);
    a.li(T0, 0xA460_0000); // PI
    a.sw(ZERO, 0, T0); // PI_DRAM_ADDR = 0
    a.li(T1, 0x1000_1000);
    a.sw(T1, 4, T0); // PI_CART_ADDR
    a.li(T1, IMAGE_LEN as u32 - 1);
    a.sw(T1, 0xC, T0); // PI_WR_LEN: cart -> RDRAM
    a.label("wait");
    a.lw(T1, 0x10, T0);
    a.andi(T1, T1, 3);
    a.bne(T1, ZERO, "wait");
    a.nop();
    a.li(T0, 0x8000_0400);
    a.jr(T0);
    a.nop();
    a.finish()
}

fn handler() -> Vec<u32> {
    let mut a = Asm::new(0x8000_0180);
    a.lui(K0, 0xA430); // MI
    a.lw(K1, 8, K0); // MI_INTR
    a.andi(K1, K1, 0x08); // VI
    a.beq(K1, ZERO, "no_vi");
    a.nop();
    a.lui(K0, 0xA440);
    a.sw(ZERO, 0x10, K0); // VI_CURRENT: acknowledge
    a.label("no_vi");
    a.lui(K0, 0xA430);
    a.lw(K1, 8, K0);
    a.andi(K1, K1, 0x20); // DP
    a.beq(K1, ZERO, "no_dp");
    a.nop();
    a.ori(K1, ZERO, 0x800); // MI_MODE: clear DP interrupt
    a.sw(K1, 0, K0);
    a.label("no_dp");
    a.lui(K0, 0x8000);
    a.lw(K1, 0x100, K0);
    a.addiu(K1, K1, 1);
    a.sw(K1, 0x100, K0);
    a.eret();
    a.finish()
}

fn sp_dma(a: &mut Asm, mem: u32, dram: u32) {
    a.li(T0, 0xA404_0000);
    a.li(T1, mem);
    a.sw(T1, 0, T0);
    a.li(T1, dram);
    a.sw(T1, 4, T0);
    a.li(T1, 0xFFF);
    a.sw(T1, 8, T0); // SP_RD_LEN: RDRAM -> SP memory
    a.label(if mem == 0x1000 { "imem_wait" } else { "dmem_wait" });
    a.lw(T1, 0x18, T0); // SP_DMA_BUSY
    a.bne(T1, ZERO, if mem == 0x1000 { "imem_wait" } else { "dmem_wait" });
    a.nop();
}

fn main_code() -> Vec<u32> {
    let mut a = Asm::new(0x8000_0400);
    // COP0 Status: CU1 | CU0 | FR | IM2 | IE
    a.li(T0, 0x3400_0401);
    a.mtc0(T0, 12);
    a.ctc1(ZERO, 31); // FCR31: round to nearest, no exceptions enabled
    // MI: unmask VI and DP
    a.li(T0, 0xA430_000C);
    a.li(T1, (1 << 7) | (1 << 11));
    a.sw(T1, 0, T0);
    // VI: 16-bit, 525 lines, an interrupt at line 2
    a.li(T0, 0xA440_0000);
    for (reg, value) in [
        (0x00, 0x0000_3002u32),
        (0x04, 0x0010_0000),
        (0x08, 320),
        (0x0C, 2),
        (0x14, 0x03E5_2239),
        (0x18, 0x20D),
        (0x1C, 0x0000_0C15),
        (0x20, 0x0C15_0C15),
        (0x24, 0x006C_02EC),
        (0x28, 0x0025_01FF),
        (0x2C, 0x000E_0204),
        (0x30, 0x200),
        (0x34, 0x400),
    ] {
        a.li(T1, value);
        a.sw(T1, reg, T0);
    }
    sp_dma(&mut a, 0x1000, 0x2000);
    sp_dma(&mut a, 0x0000, 0x3000);
    a.li(S0, 0x1234_5678); // checksum
    a.ori(S1, ZERO, 0); // round
    a.li(S2, 0x8000_5000); // data, cached
    a.li(S3, 0xA000_5000); // the same data, uncached
    a.ori(S4, ZERO, 0); // index
    a.li(S5, 0x0F0F_1357);

    // (cpuopt) TLB entry 0: virtual 0x406000/0x407000 -> physical 0x6000/0x7000, cached
    a.mtc0(ZERO, 0); // Index
    a.li(T0, 0x19F);
    a.mtc0(T0, 2); // EntryLo0
    a.li(T0, 0x1DF);
    a.mtc0(T0, 3); // EntryLo1
    a.mtc0(ZERO, 5); // PageMask
    a.li(T0, 0x0040_6000);
    a.mtc0(T0, 10); // EntryHi
    a.w(0x4200_0002); // tlbwi
    a.label("loop");
    a.ori(T9, ZERO, 48); // CPU block rounds
    a.label("cpu");
    a.addu(GP, S2, S4);
    a.lw(T0, 0, GP);
    a.lw(T1, 4, GP);
    a.addu(T2, T0, T1);
    a.subu(T3, T0, T1);
    a.xor(T4, T2, T3);
    a.nor(T5, T4, S0);
    a.sllv(T6, T4, T1);
    a.srav(T7, T5, T0);
    a.srlv(T8, T2, S1);
    a.slt(V0, T0, T1);
    a.sltu(V1, T0, T1);
    a.addu(S0, S0, V0);
    a.xor(S0, S0, V1);
    a.mult(T0, T1);
    a.mflo(A0);
    a.mfhi(A1);
    a.ori(A2, T1, 1);
    a.divu(T0, A2);
    a.mflo(A3);
    a.mfhi(FP);
    a.addu(S0, S0, A0);
    a.xor(S0, S0, A1);
    a.addu(S0, S0, A3);
    a.xor(S0, S0, FP);
    a.div(T2, A2);
    a.mflo(A3);
    a.subu(S0, S0, A3);
    // 64-bit
    a.dsll32(T6, T0, 0);
    a.daddu(T6, T6, T1);
    a.dmult(T6, T4);
    a.mflo(A0);
    a.mfhi(A1);
    a.dsra(A1, A1, 7);
    a.ori(A2, T5, 3);
    a.ddivu(A0, A2);
    a.mflo(A3);
    a.ddiv(T6, A2);
    a.mfhi(T7);
    a.dsubu(A3, A3, T7);
    a.dsrl32(T8, A3, 3);
    a.dsllv(A0, A0, T1);
    a.dsrav(A0, A0, T0);
    a.xor(S0, S0, A0);
    a.addu(S0, S0, A1);
    a.xor(S0, S0, T8);
    a.sd(A3, 8, GP);
    a.ld(A1, 8, GP);
    a.daddu(S5, S5, A1);
    // narrow and unaligned loads and stores
    a.lb(A0, 1, GP);
    a.lbu(A1, 2, GP);
    a.lh(A2, 2, GP);
    a.lhu(A3, 6, GP);
    a.addu(T7, A0, A1);
    a.addu(T7, T7, A2);
    a.xor(T7, T7, A3);
    a.lwl(T8, 1, GP);
    a.lwr(T8, 4, GP);
    a.addu(S0, S0, T7);
    a.xor(S0, S0, T8);
    a.sb(S0, 3, GP);
    a.sh(T7, 0x12, GP);
    a.addu(T5, S3, S4);
    a.lw(T6, 0x20, T5); // uncached read of a cached line's backing memory
    a.xor(S0, S0, T6);
    a.sw(T4, 0x24, T5); // uncached write
    // COP1
    a.mtc1(T0, 0);
    a.fpu(20, 33, 2, 0, 0); // cvt.d.w f2, f0
    a.andi(T6, T1, 0x7FFF);
    a.ori(T6, T6, 1);
    a.mtc1(T6, 4);
    a.fpu(20, 33, 6, 4, 0); // cvt.d.w f6, f4
    a.fpu(17, 0, 8, 2, 6); // add.d f8, f2, f6
    a.fpu(17, 2, 10, 8, 2); // mul.d f10, f8, f2
    a.fpu(17, 3, 12, 10, 6); // div.d f12, f10, f6
    a.fpu(17, 5, 14, 12, 0); // abs.d f14, f12
    a.fpu(17, 4, 16, 14, 0); // sqrt.d f16, f14
    a.fpu(17, 1, 18, 16, 8); // sub.d f18, f16, f8
    a.fpu(17, 32, 20, 18, 0); // cvt.s.d f20, f18
    a.fpu(16, 2, 22, 20, 20); // mul.s f22, f20, f20
    a.fpu(16, 0, 24, 22, 20); // add.s f24, f22, f20
    a.fpu(17, 13, 26, 6, 0); // trunc.w.d f26, f6 (in range)
    a.mfc1(A0, 26);
    a.dmfc1(A1, 18);
    a.mfc1(A2, 24);
    a.addu(S0, S0, A0);
    a.xor(S0, S0, A1);
    a.dsrl32(A1, A1, 0);
    a.addu(S0, S0, A1);
    a.xor(S0, S0, A2);
    a.cfc1(A3, 31); // the flags the arithmetic raised
    a.addu(S0, S0, A3);
    a.sdc1(16, 0x18, GP);
    a.fpu(17, 60, 0, 2, 6); // c.lt.d f2, f6
    a.bc1t("flt");
    a.nop();
    a.xori(S0, S0, 0x5A5A);
    a.label("flt");
    // branches on the data
    a.bltz(T4, "neg");
    a.addiu(S0, S0, 7); // delay slot
    a.xori(S0, S0, 0x1111);
    a.label("neg");
    a.beql(V0, ZERO, "skip_likely");
    a.addiu(S0, S0, 3); // likely delay slot: only on taken
    a.label("skip_likely");
    // store the mix back, advance
    a.sw(S0, 0, GP);
    a.sw(T4, 4, GP);
    a.addiu(S4, S4, 8);
    a.andi(S4, S4, 0x3F8);
    a.addiu(T9, T9, -1);
    a.bgtz(T9, "cpu");
    a.nop();

    // the RSP: when halted, take its results and start it again
    a.li(T0, 0xA404_0010);
    a.lw(T1, 0, T0); // SP_STATUS
    a.andi(T1, T1, 1);
    a.beq(T1, ZERO, "rsp_busy");
    a.nop();
    a.li(T2, 0xA400_0000);
    a.lw(T3, 0xF00, T2);
    a.lw(T4, 0xF04, T2);
    a.lw(T5, 0x200, T2);
    a.lw(T6, 0x2F0, T2);
    a.lw(T7, 0x3C0, T2);
    a.lw(T8, 0x3CC, T2);
    a.lw(A0, 0xF08, T2);
    a.addu(S0, S0, T7);
    a.xor(S0, S0, T8);
    a.addu(S0, S0, A0);
    a.addu(S0, S0, T3);
    a.xor(S0, S0, T4);
    a.addu(S0, S0, T5);
    a.xor(S0, S0, T6);
    a.sw(S0, 0xE00, T2);
    a.sw(S5, 0xE04, T2);
    a.sw(S1, 0xE08, T2);
    a.li(T2, 0xA408_0000);
    a.sw(ZERO, 0, T2); // SP_PC = 0
    a.ori(T1, ZERO, 0x5); // clear HALT and BROKE
    a.sw(T1, 0, T0);
    a.label("rsp_busy");

    // the RDP: a fill and a SyncFull every 16 rounds
    a.andi(T1, S1, 15);
    a.bne(T1, ZERO, "no_rdp");
    a.nop();
    a.li(T0, 0xA410_0000);
    a.li(T1, 0x4000);
    a.sw(T1, 0, T0); // DPC_START
    a.li(T1, 0x4000 + 5 * 8);
    a.sw(T1, 4, T0); // DPC_END
    a.label("no_rdp");

    // (cpuopt) code written at run time and the instruction cache
    a.li(T0, 0xA000_6000);
    a.andi(T2, S1, 0x7FFF);
    a.li(T1, 0x2610_0000);
    a.or(T1, T1, T2);
    a.sw(T1, 0, T0); // addiu s0, s0, round
    a.li(T1, r(38, S0, S1, S0, 0));
    a.sw(T1, 4, T0); // xor s0, s0, s1
    a.li(T1, r(8, RA, 0, 0, 0));
    a.sw(T1, 8, T0); // jr ra
    a.sw(ZERO, 0xC, T0);
    // a routine that invalidates its own line, then runs the word after it (changed each round)
    a.li(T1, i(47, T3, 0x10, 0x20));
    a.sw(T1, 0x20, T0); // cache hit invalidate, 0x20(t3)
    a.xori(T2, T2, 0x5A5A);
    a.li(T1, 0x2610_0000);
    a.or(T1, T1, T2);
    a.sw(T1, 0x24, T0); // addiu s0, s0, ...
    a.li(T1, r(8, RA, 0, 0, 0));
    a.sw(T1, 0x28, T0);
    a.sw(ZERO, 0x2C, T0);
    // a routine in the TLB-mapped page that remaps its own page, then runs on in the same line
    a.li(T1, 0x4080_0000 | (T7 << 16) | (2 << 11));
    a.sw(T1, 0x40, T0); // mtc0 t7, EntryLo0
    a.li(T1, 0x4200_0002);
    a.sw(T1, 0x44, T0); // tlbwi
    a.li(T1, 0x2610_0011);
    a.sw(T1, 0x48, T0); // addiu s0, s0, 0x11 (this page)
    a.li(T1, r(8, RA, 0, 0, 0));
    a.sw(T1, 0x4C, T0);
    a.sw(ZERO, 0x50, T0);
    a.li(T1, 0x2610_2222);
    a.sw(T1, 0x1048, T0); // addiu s0, s0, 0x2222 (the page it maps instead)
    a.li(T1, r(8, RA, 0, 0, 0));
    a.sw(T1, 0x104C, T0);
    a.sw(ZERO, 0x1050, T0);
    a.li(T3, 0x8000_6000);
    a.jalr(RA, T3); // may run last round's code from the cache
    a.nop();
    a.andi(T2, S1, 3);
    a.bne(T2, ZERO, "c1");
    a.nop();
    a.cache(0x10, 0, T3); // I hit invalidate
    a.label("c1");
    a.ori(T1, ZERO, 1);
    a.bne(T2, T1, "c2");
    a.nop();
    a.cache(0x00, 0, T3); // I index invalidate
    a.label("c2");
    a.ori(T1, ZERO, 2);
    a.bne(T2, T1, "c3");
    a.nop();
    a.cache(0x14, 0, T3); // I fill
    a.label("c3");
    a.ori(T1, ZERO, 3);
    a.bne(T2, T1, "c4");
    a.nop();
    a.mtc0(ZERO, 28); // TagLo = 0
    a.cache(0x08, 0, T3); // I store tag: invalid
    a.cache(0x04, 0, T3); // I load tag
    a.mfc0(T1, 28);
    a.addu(S0, S0, T1);
    a.label("c4");
    a.jalr(RA, T3);
    a.nop();
    a.li(T3, 0x0040_6000); // the same code through the TLB
    a.jalr(RA, T3);
    a.nop();
    a.li(T3, 0x8000_6000);
    a.li(T1, 0x8000_6020);
    a.jalr(RA, T1); // invalidates its own line
    a.nop();
    a.li(T3, 0x0040_6000);
    a.li(T1, 0x0040_6020);
    a.jalr(RA, T1); // the same through the TLB
    a.nop();
    a.li(T7, 0x1DF); // EntryLo0: physical 0x7000
    a.li(T1, 0x0040_6040);
    a.jalr(RA, T1); // remaps its own page
    a.nop();
    a.li(T7, 0x19F); // back to physical 0x6000
    a.mtc0(T7, 2);
    a.w(0x4200_0002); // tlbwi
    // (cpuopt) data cache misses and dirty writebacks: 4 KiB copied between two regions that
    // share the cache's lines, at a 16-byte stride, the source made dirty too
    a.andi(T5, S1, 0xF);
    a.sll(T5, T5, 12);
    a.li(T0, 0x8010_0000);
    a.addu(T0, T0, T5);
    a.li(T1, 0x8020_0000);
    a.addu(T1, T1, T5);
    a.ori(T2, ZERO, 256);
    a.label("mloop");
    a.lw(T3, 0, T0);
    a.addu(T3, T3, S0);
    a.sw(T3, 0, T1);
    a.sw(T3, 4, T0);
    a.ld(T6, 8, T1);
    a.daddu(T6, T6, T3);
    a.sd(T6, 8, T0);
    a.addiu(T0, T0, 16);
    a.addiu(T1, T1, 16);
    a.addiu(T2, T2, -1);
    a.bgtz(T2, "mloop");
    a.nop();
    a.xor(S0, S0, T3);
    a.xor(S0, S0, T6);
    a.lui(T0, 0x8000);
    a.sw(S0, 0x104, T0);
    a.lw(T1, 0x100, T0); // interrupts taken
    a.addu(S0, S0, T1);
    a.addiu(S1, S1, 1);
    a.beq(ZERO, ZERO, "loop");
    a.nop();
    a.finish()
}

fn rsp_code() -> Vec<u32> {
    let mut a = Asm::new(0);
    // inputs v0..v7 from DMEM 0x000
    for v in 0..8 {
        a.vls(false, 4, v, 0, v as i32, 0); // lqv
    }
    a.lw(1, 0xE00, 0);
    a.lw(2, 0xE04, 0);
    a.lw(3, 0xE08, 0);
    a.mtc2(1, 8, 0);
    a.mtc2(2, 8, 6);
    a.mtc2(3, 8, 12);
    a.mtc2(1, 8, 3); // odd element: straddles two lanes
    // multiplies and accumulates, with every kind of element selector
    let ops: &[(u32, u32, u32, u32, u32)] = &[
        (7, 9, 0, 1, 0),    // vmudh
        (15, 9, 2, 8, 8),   // vmadh
        (0, 10, 1, 2, 2),   // vmulf
        (8, 10, 3, 4, 5),   // vmacf
        (4, 11, 4, 5, 0),   // vmudl
        (13, 11, 5, 6, 9),  // vmadm
        (14, 12, 6, 7, 3),  // vmadn
        (1, 13, 0, 7, 1),   // vmulu
        (9, 13, 1, 8, 15),  // vmacu
        (6, 14, 2, 3, 4),   // vmudn
        (5, 15, 3, 2, 10),  // vmudm
        (12, 15, 4, 1, 11), // vmadl
        (3, 16, 5, 0, 0),   // vmulq
        (11, 16, 6, 7, 0),  // vmacq
        (2, 17, 7, 8, 12),  // vrndp
        (10, 17, 0, 8, 13), // vrndn
        (29, 18, 0, 0, 8),  // vsar high
        (29, 19, 0, 0, 9),  // vsar mid
        (29, 20, 0, 0, 10), // vsar low
        (16, 21, 0, 1, 0),  // vadd
        (17, 22, 9, 2, 2),  // vsub
        (20, 23, 10, 3, 0), // vaddc
        (21, 24, 11, 4, 7), // vsubc
        (16, 23, 23, 0, 0), // vadd with the carries
        (19, 25, 12, 13, 0),// vabs
        (32, 26, 0, 1, 0),  // vlt
        (33, 27, 2, 3, 3),  // veq
        (34, 27, 4, 5, 6),  // vne
        (35, 28, 6, 7, 14), // vge
        (36, 29, 9, 10, 0), // vcl
        (37, 29, 11, 12, 2),// vch
        (36, 29, 13, 14, 0),// vcl after vch
        (38, 30, 15, 16, 5),// vcr
        (39, 31, 17, 18, 0),// vmrg
        (40, 1, 0, 9, 0),   // vand
        (41, 2, 1, 10, 8),  // vnand
        (42, 3, 2, 11, 0),  // vor
        (43, 4, 3, 12, 0),  // vnor
        (44, 5, 4, 13, 0),  // vxor
        (45, 6, 5, 14, 0),  // vnxor
        (48, 7, 0, 9, 3),   // vrcp (de = vs field)
        (50, 7, 1, 9, 4),   // vrcph
        (49, 8, 2, 10, 5),  // vrcpl
        (52, 12, 3, 11, 6), // vrsq
        (54, 12, 4, 11, 7), // vrsqh
        (53, 13, 5, 12, 8), // vrsql
        (51, 14, 6, 15, 9), // vmov
        (0, 15, 21, 22, 0), // vmulf
        (8, 15, 25, 26, 0), // vmacf
    ];
    // after each op, its result is folded into v30 (and the accumulator, through vsar, after a
    // multiply; the flags, through cfc2, after an add, compare or clip) so no output is lost
    a.vu(44, 30, 30, 30, 0); // vxor v30 = 0
    a.ori(23, 0, 0);
    for &(funct, vd, vs, vt, e) in ops {
        let vd = if vd >= 30 { vd - 10 } else { vd };
        a.vu(funct, vd, vs, vt, e);
        a.vu(44, 30, 30, vd, 0); // vxor v30, v30, vd
        if funct < 16 {
            for sel in 8..11 {
                a.vu(29, 31, 0, 0, sel); // vsar v31
                a.vu(16, 30, 30, 31, 0); // vadd v30, v30, v31 (also reads and clears VCO)
            }
        } else if (16..40).contains(&funct) {
            for reg in 0..3 {
                a.cfc2(20, reg);
                a.sll(21, 23, 5);
                a.srl(23, 23, 27);
                a.or(23, 23, 21);
                a.xor(23, 23, 20);
            }
        }
    }
    a.vls(true, 4, 30, 0, 0x3C, 0); // sqv v30 -> 0x3C0
    a.sw(23, 0xF08, 0);
    a.cfc2(4, 0); // VCO
    a.cfc2(5, 1); // VCC
    a.cfc2(6, 2); // VCE
    a.mfc2(7, 29, 4);
    // results to DMEM 0x200.., a few back into the inputs for the next task
    for v in 0..16 {
        a.vls(true, 4, 16 + v, 0, 0x20 + v as i32, 0); // sqv
    }
    a.vls(true, 4, 21, 0, 0, 0);
    a.vls(true, 4, 29, 0, 1, 0);
    a.vls(true, 4, 9, 0, 2, 0);
    a.vls(true, 4, 15, 0, 3, 0);
    a.vls(true, 4, 30, 0, 4, 0);
    // other loads and stores, aligned and not
    a.ori(8, 0, 0x0105);
    a.vls(false, 3, 1, 8, 0, 8); // ldv at an odd address
    a.vls(false, 0, 2, 3, 1, 8); // lbv
    a.vls(false, 1, 3, 6, 1, 8); // lsv
    a.vls(false, 2, 4, 4, 1, 8); // llv
    a.vls(false, 5, 5, 0, 1, 8); // lrv
    a.vls(false, 6, 6, 0, 0, 8); // lpv
    a.vls(false, 7, 7, 0, 0, 8); // luv
    a.vls(false, 8, 10, 0, 0, 8); // lhv
    a.vls(false, 9, 11, 0, 0, 8); // lfv
    a.vls(false, 11, 16, 0, 0, 8); // ltv
    a.ori(15, 0, 0x0300);
    a.vls(true, 6, 6, 0, 0x10, 15); // spv  0x380
    a.vls(true, 7, 7, 0, 0x11, 15); // suv  0x388
    a.vls(true, 8, 10, 0, 0x2A, 0); // shv  0x2A0
    a.vls(true, 9, 11, 0, 0x2C, 0); // sfv  0x2C0
    a.vls(true, 10, 3, 2, 0x2E, 0); // swv  0x2E0
    a.vls(true, 11, 16, 0, 0x30, 0); // stv 0x300
    a.vls(true, 3, 1, 3, 0x12, 15); // sdv  0x390
    a.vls(true, 2, 4, 1, 0x28, 15); // slv  0x3A0
    a.vls(true, 1, 5, 5, 0x30, 8); // ssv   0x165 (unaligned)
    a.vls(true, 0, 2, 7, 0x3F, 8); // sbv   0x144
    // a scalar loop over the results
    a.ori(9, 0, 0x200);
    a.ori(10, 0, 0);
    a.label("sum");
    a.lw(11, 0, 9);
    a.addu(10, 10, 11);
    a.sll(12, 10, 3);
    a.srl(13, 10, 29);
    a.or(10, 12, 13);
    a.xor(10, 10, 4);
    a.addiu(9, 9, 4);
    a.slti(14, 9, 0x380);
    a.bne(14, 0, "sum");
    a.nop();
    a.addu(10, 10, 5);
    a.addu(10, 10, 6);
    a.addu(10, 10, 7);
    a.sw(10, 0xF00, 0);
    a.sh(1, 0xF04, 0);
    a.sb(2, 0xF06, 0);
    a.brk();
    a.nop();
    a.finish()
}

fn rdp_list() -> Vec<u32> {
    vec![
        0x2F00_0000 | (3 << 20), 0,                                 // SetOtherModes: fill
        0x2D00_0000, ((320 << 2) << 12) | (240 << 2),               // SetScissor
        0x3F00_0000 | (2 << 19) | 319, 0x0010_0000,                 // SetColorImage
        0x3600_0000 | ((319 << 2) << 12) | (239 << 2), 0,           // FillRectangle
        0x2900_0000, 0,                                             // SyncFull
    ]
}

pub fn rom() -> Vec<u8> {
    let mut rom = vec![0u8; 0x1000 + IMAGE_LEN];
    put(&mut rom, 0, &[0x8037_1240, 0x0000_000F, 0x8000_0400, 0x0000_144C, 0x4C4F_434B, 0x5354_4550]);
    rom[0x20..0x34].copy_from_slice(b"LOCKSTEP SYNTHETIC  ");
    rom[0x3B..0x3F].copy_from_slice(b"NLSE");
    put(&mut rom, 0x40, &boot_code());
    let image = 0x1000;
    put(&mut rom, image + 0x180, &handler());
    put(&mut rom, image + 0x400, &main_code());
    let rsp = rsp_code();
    assert!(rsp.len() * 4 <= 0x1000);
    put(&mut rom, image + 0x2000, &rsp);
    // the DMEM image and the CPU's data: a fixed pseudo-random fill
    let mut x: u64 = 0x9E37_79B9_7F4A_7C15;
    for k in (0x3000..0x3400).chain(0x5000..0x5400) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rom[image + k] = (x >> 24) as u8;
    }
    put(&mut rom, image + 0x4000, &rdp_list());
    rom
}
