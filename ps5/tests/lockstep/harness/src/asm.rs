// A small MIPS assembler for the synthetic test ROM (VR4300 and RSP encodings), with labels.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
#![allow(dead_code)]

pub const ZERO: u32 = 0;
pub const AT: u32 = 1;
pub const V0: u32 = 2;
pub const V1: u32 = 3;
pub const A0: u32 = 4;
pub const A1: u32 = 5;
pub const A2: u32 = 6;
pub const A3: u32 = 7;
pub const T0: u32 = 8;
pub const T1: u32 = 9;
pub const T2: u32 = 10;
pub const T3: u32 = 11;
pub const T4: u32 = 12;
pub const T5: u32 = 13;
pub const T6: u32 = 14;
pub const T7: u32 = 15;
pub const S0: u32 = 16;
pub const S1: u32 = 17;
pub const S2: u32 = 18;
pub const S3: u32 = 19;
pub const S4: u32 = 20;
pub const S5: u32 = 21;
pub const S6: u32 = 22;
pub const S7: u32 = 23;
pub const T8: u32 = 24;
pub const T9: u32 = 25;
pub const K0: u32 = 26;
pub const K1: u32 = 27;
pub const GP: u32 = 28;
pub const SP: u32 = 29;
pub const FP: u32 = 30;
pub const RA: u32 = 31;

enum Fixup {
    Branch(usize, &'static str),
}

/// Code at a fixed virtual base address; branches resolve to labels at `finish`.
pub struct Asm {
    pub base: u32,
    pub words: Vec<u32>,
    labels: Vec<(&'static str, usize)>,
    fixups: Vec<Fixup>,
}

pub fn r(funct: u32, rs: u32, rt: u32, rd: u32, sa: u32) -> u32 {
    (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct
}
pub fn i(op: u32, rs: u32, rt: u32, imm: u32) -> u32 {
    (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
}

impl Asm {
    pub fn new(base: u32) -> Asm {
        Asm { base, words: Vec::new(), labels: Vec::new(), fixups: Vec::new() }
    }
    pub fn here(&self) -> u32 {
        self.base + 4 * self.words.len() as u32
    }
    pub fn label(&mut self, name: &'static str) {
        self.labels.push((name, self.words.len()));
    }
    pub fn w(&mut self, word: u32) {
        self.words.push(word);
    }
    fn branch(&mut self, word: u32, target: &'static str) {
        self.fixups.push(Fixup::Branch(self.words.len(), target));
        self.words.push(word);
    }
    pub fn finish(mut self) -> Vec<u32> {
        for f in &self.fixups {
            let Fixup::Branch(at, name) = f;
            let to = self
                .labels
                .iter()
                .find(|(n, _)| n == name)
                .unwrap_or_else(|| panic!("asm: no label {name}"))
                .1;
            let off = to as i64 - (*at as i64 + 1);
            assert!((-32768..32768).contains(&off), "asm: branch to {name} out of range");
            self.words[*at] |= (off as u32) & 0xFFFF;
        }
        std::mem::take(&mut self.words)
    }

    // --- VR4300 / RSP scalar ---
    pub fn nop(&mut self) {
        self.w(0)
    }
    /// load a 32-bit constant (lui + ori)
    pub fn li(&mut self, rt: u32, value: u32) {
        self.w(i(15, 0, rt, value >> 16));
        self.w(i(13, rt, rt, value));
    }
    pub fn addiu(&mut self, rt: u32, rs: u32, imm: i32) {
        self.w(i(9, rs, rt, imm as u32))
    }
    pub fn addi(&mut self, rt: u32, rs: u32, imm: i32) {
        self.w(i(8, rs, rt, imm as u32))
    }
    pub fn daddiu(&mut self, rt: u32, rs: u32, imm: i32) {
        self.w(i(25, rs, rt, imm as u32))
    }
    pub fn andi(&mut self, rt: u32, rs: u32, imm: u32) {
        self.w(i(12, rs, rt, imm))
    }
    pub fn ori(&mut self, rt: u32, rs: u32, imm: u32) {
        self.w(i(13, rs, rt, imm))
    }
    pub fn xori(&mut self, rt: u32, rs: u32, imm: u32) {
        self.w(i(14, rs, rt, imm))
    }
    pub fn slti(&mut self, rt: u32, rs: u32, imm: i32) {
        self.w(i(10, rs, rt, imm as u32))
    }
    pub fn sltiu(&mut self, rt: u32, rs: u32, imm: i32) {
        self.w(i(11, rs, rt, imm as u32))
    }
    pub fn lui(&mut self, rt: u32, imm: u32) {
        self.w(i(15, 0, rt, imm))
    }
    pub fn lw(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(35, base, rt, off as u32))
    }
    pub fn lwu(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(39, base, rt, off as u32))
    }
    pub fn lh(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(33, base, rt, off as u32))
    }
    pub fn lhu(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(37, base, rt, off as u32))
    }
    pub fn lb(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(32, base, rt, off as u32))
    }
    pub fn lbu(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(36, base, rt, off as u32))
    }
    pub fn ld(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(55, base, rt, off as u32))
    }
    pub fn lwl(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(34, base, rt, off as u32))
    }
    pub fn lwr(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(38, base, rt, off as u32))
    }
    pub fn sw(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(43, base, rt, off as u32))
    }
    pub fn sh(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(41, base, rt, off as u32))
    }
    pub fn sb(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(40, base, rt, off as u32))
    }
    pub fn sd(&mut self, rt: u32, off: i32, base: u32) {
        self.w(i(63, base, rt, off as u32))
    }
    pub fn ldc1(&mut self, ft: u32, off: i32, base: u32) {
        self.w(i(53, base, ft, off as u32))
    }
    pub fn sdc1(&mut self, ft: u32, off: i32, base: u32) {
        self.w(i(61, base, ft, off as u32))
    }
    pub fn cache(&mut self, op: u32, off: i32, base: u32) {
        self.w(i(47, base, op, off as u32))
    }
    pub fn beq(&mut self, rs: u32, rt: u32, to: &'static str) {
        self.branch(i(4, rs, rt, 0), to)
    }
    pub fn bne(&mut self, rs: u32, rt: u32, to: &'static str) {
        self.branch(i(5, rs, rt, 0), to)
    }
    pub fn beql(&mut self, rs: u32, rt: u32, to: &'static str) {
        self.branch(i(20, rs, rt, 0), to)
    }
    pub fn bnel(&mut self, rs: u32, rt: u32, to: &'static str) {
        self.branch(i(21, rs, rt, 0), to)
    }
    pub fn blez(&mut self, rs: u32, to: &'static str) {
        self.branch(i(6, rs, 0, 0), to)
    }
    pub fn bgtz(&mut self, rs: u32, to: &'static str) {
        self.branch(i(7, rs, 0, 0), to)
    }
    pub fn bltz(&mut self, rs: u32, to: &'static str) {
        self.branch(i(1, rs, 0, 0), to)
    }
    pub fn bgez(&mut self, rs: u32, to: &'static str) {
        self.branch(i(1, rs, 1, 0), to)
    }
    pub fn j_abs(&mut self, target: u32) {
        self.w((2 << 26) | ((target >> 2) & 0x03FF_FFFF))
    }
    pub fn jr(&mut self, rs: u32) {
        self.w(r(8, rs, 0, 0, 0))
    }
    pub fn jalr(&mut self, rd: u32, rs: u32) {
        self.w(r(9, rs, 0, rd, 0))
    }
    pub fn brk(&mut self) {
        self.w(r(13, 0, 0, 0, 0))
    }
    // SPECIAL ALU, rd = rs op rt
    pub fn sp3(&mut self, funct: u32, rd: u32, rs: u32, rt: u32) {
        self.w(r(funct, rs, rt, rd, 0))
    }
    pub fn addu(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(33, rd, rs, rt)
    }
    pub fn subu(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(35, rd, rs, rt)
    }
    pub fn and(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(36, rd, rs, rt)
    }
    pub fn or(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(37, rd, rs, rt)
    }
    pub fn xor(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(38, rd, rs, rt)
    }
    pub fn nor(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(39, rd, rs, rt)
    }
    pub fn slt(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(42, rd, rs, rt)
    }
    pub fn sltu(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(43, rd, rs, rt)
    }
    pub fn daddu(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(45, rd, rs, rt)
    }
    pub fn dsubu(&mut self, rd: u32, rs: u32, rt: u32) {
        self.sp3(47, rd, rs, rt)
    }
    pub fn sllv(&mut self, rd: u32, rt: u32, rs: u32) {
        self.sp3(4, rd, rs, rt)
    }
    pub fn srlv(&mut self, rd: u32, rt: u32, rs: u32) {
        self.sp3(6, rd, rs, rt)
    }
    pub fn srav(&mut self, rd: u32, rt: u32, rs: u32) {
        self.sp3(7, rd, rs, rt)
    }
    pub fn dsllv(&mut self, rd: u32, rt: u32, rs: u32) {
        self.sp3(20, rd, rs, rt)
    }
    pub fn dsrav(&mut self, rd: u32, rt: u32, rs: u32) {
        self.sp3(23, rd, rs, rt)
    }
    pub fn shift(&mut self, funct: u32, rd: u32, rt: u32, sa: u32) {
        self.w(r(funct, 0, rt, rd, sa))
    }
    pub fn sll(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(0, rd, rt, sa)
    }
    pub fn srl(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(2, rd, rt, sa)
    }
    pub fn sra(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(3, rd, rt, sa)
    }
    pub fn dsll32(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(60, rd, rt, sa)
    }
    pub fn dsrl32(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(62, rd, rt, sa)
    }
    pub fn dsra(&mut self, rd: u32, rt: u32, sa: u32) {
        self.shift(59, rd, rt, sa)
    }
    pub fn muldiv(&mut self, funct: u32, rs: u32, rt: u32) {
        self.w(r(funct, rs, rt, 0, 0))
    }
    pub fn mult(&mut self, rs: u32, rt: u32) {
        self.muldiv(24, rs, rt)
    }
    pub fn multu(&mut self, rs: u32, rt: u32) {
        self.muldiv(25, rs, rt)
    }
    pub fn div(&mut self, rs: u32, rt: u32) {
        self.muldiv(26, rs, rt)
    }
    pub fn divu(&mut self, rs: u32, rt: u32) {
        self.muldiv(27, rs, rt)
    }
    pub fn dmult(&mut self, rs: u32, rt: u32) {
        self.muldiv(28, rs, rt)
    }
    pub fn dmultu(&mut self, rs: u32, rt: u32) {
        self.muldiv(29, rs, rt)
    }
    pub fn ddiv(&mut self, rs: u32, rt: u32) {
        self.muldiv(30, rs, rt)
    }
    pub fn ddivu(&mut self, rs: u32, rt: u32) {
        self.muldiv(31, rs, rt)
    }
    pub fn mfhi(&mut self, rd: u32) {
        self.w(r(16, 0, 0, rd, 0))
    }
    pub fn mflo(&mut self, rd: u32) {
        self.w(r(18, 0, 0, rd, 0))
    }
    pub fn mtc0(&mut self, rt: u32, rd: u32) {
        self.w(0x4080_0000 | (rt << 16) | (rd << 11))
    }
    pub fn mfc0(&mut self, rt: u32, rd: u32) {
        self.w(0x4000_0000 | (rt << 16) | (rd << 11))
    }
    pub fn eret(&mut self) {
        self.w(0x4200_0018)
    }
    // --- COP1 ---
    pub fn mtc1(&mut self, rt: u32, fs: u32) {
        self.w(0x4480_0000 | (rt << 16) | (fs << 11))
    }
    pub fn mfc1(&mut self, rt: u32, fs: u32) {
        self.w(0x4400_0000 | (rt << 16) | (fs << 11))
    }
    pub fn dmfc1(&mut self, rt: u32, fs: u32) {
        self.w(0x4420_0000 | (rt << 16) | (fs << 11))
    }
    pub fn ctc1(&mut self, rt: u32, fs: u32) {
        self.w(0x44C0_0000 | (rt << 16) | (fs << 11))
    }
    pub fn cfc1(&mut self, rt: u32, fs: u32) {
        self.w(0x4440_0000 | (rt << 16) | (fs << 11))
    }
    /// fmt: 16 single, 17 double, 20 word, 21 long
    pub fn fpu(&mut self, fmt: u32, funct: u32, fd: u32, fs: u32, ft: u32) {
        self.w(0x4400_0000 | (fmt << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct)
    }
    pub fn bc1t(&mut self, to: &'static str) {
        self.branch(0x4501_0000, to)
    }
    pub fn bc1f(&mut self, to: &'static str) {
        self.branch(0x4500_0000, to)
    }
    // --- RSP COP2 ---
    /// vector ALU: vd = vs op vt[e]
    pub fn vu(&mut self, funct: u32, vd: u32, vs: u32, vt: u32, e: u32) {
        self.w(vu(funct, vd, vs, vt, e))
    }
    /// LWC2 (store = false) or SWC2: kind 0 bv, 1 sv, 2 lv, 3 dv, 4 qv, 5 rv, 6 pv, 7 uv, 8 hv,
    /// 9 fv, 10 wv, 11 tv; offset in units of the access size
    pub fn vls(&mut self, store: bool, kind: u32, vt: u32, e: u32, off: i32, base: u32) {
        self.w(vls(store, kind, vt, e, off, base))
    }
    pub fn mtc2(&mut self, rt: u32, vd: u32, e: u32) {
        self.w((0x12 << 26) | (4 << 21) | (rt << 16) | (vd << 11) | (e << 7))
    }
    pub fn mfc2(&mut self, rt: u32, vd: u32, e: u32) {
        self.w((0x12 << 26) | (rt << 16) | (vd << 11) | (e << 7))
    }
    pub fn cfc2(&mut self, rt: u32, rd: u32) {
        self.w((0x12 << 26) | (2 << 21) | (rt << 16) | (rd << 11))
    }
}

pub fn vu(funct: u32, vd: u32, vs: u32, vt: u32, e: u32) -> u32 {
    (0x12 << 26) | (1 << 25) | (e << 21) | (vt << 16) | (vs << 11) | (vd << 6) | funct
}
pub fn vls(store: bool, kind: u32, vt: u32, e: u32, off: i32, base: u32) -> u32 {
    let op = if store { 0x3A } else { 0x32 };
    (op << 26) | (base << 21) | (vt << 16) | (kind << 11) | (e << 7) | (off as u32 & 0x7F)
}
