// RSP unit level: single instructions over fixed pseudo-random states, through the original's and
// the candidate's decoder and handlers, every output compared (registers, accumulators, flags,
// divide state, all of DMEM/IMEM and IMEM's pre-decoded opcodes, branch state). This finds a
// wrong SIMD rewrite of a handler without any ROM.
//
//   every vector ALU funct (0-63) x --trials states, operands, elements (0-15) and aliasing
//   every LWC2/SWC2 kind x 16 elements x 16 alignments x 4 address regions (0x000, 0x7F0,
//     0xFF0, 0xFF8: the DMEM end) + random addresses
//   mtc2: 16 elements x all 65536 values; mfc2, cfc2/ctc2
//   scalar unit: ALU, shifts, loads/stores of every width, branches and jumps
//
// SPDX-License-Identifier: GPL-3.0-or-later
use crate::asm;

#[derive(Clone, PartialEq)]
pub struct RspState {
    pub gpr: [u32; 32],
    pub vpr: [u128; 32],
    pub acc: [u128; 3],
    pub flags: [u128; 5],
    pub div: (i16, i16, bool),
    pub mem: Box<[u8; 0x2000]>,
    pub decode_imem: bool,
    pub decode: u64,
    pub branch: (u8, u32),
    pub broken: bool,
}

pub struct Rng(u64);
impl Rng {
    pub fn new(seed: u64) -> Rng {
        Rng(seed ^ 0x2545_F491_4F6C_DD1D)
    }
    pub fn next(&mut self) -> u64 {
        // splitmix64
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }
    pub fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
    /// a 16-bit lane: often a boundary value, otherwise random
    fn lane(&mut self) -> u16 {
        const EDGES: [u16; 10] = [0, 1, 2, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0xFFFE, 0x00FF, 0xFF00];
        if self.below(3) == 0 {
            EDGES[self.below(EDGES.len() as u64) as usize]
        } else {
            self.next() as u16
        }
    }
    fn vector(&mut self) -> u128 {
        let mut v = 0u128;
        for _ in 0..8 {
            v = (v << 16) | self.lane() as u128;
        }
        v
    }
    /// flag registers: lanes of all-ones or zero, as Gopher64 keeps them
    fn flag(&mut self) -> u128 {
        let mut v = 0u128;
        for _ in 0..8 {
            v = (v << 16) | if self.below(2) == 0 { 0xFFFF } else { 0 };
        }
        v
    }
}

fn random_state(rng: &mut Rng, decode_imem: bool) -> RspState {
    let mut gpr = [0u32; 32];
    for g in gpr.iter_mut().skip(1) {
        *g = if rng.below(4) == 0 { rng.lane() as i16 as i32 as u32 } else { rng.next() as u32 };
    }
    let mut mem = Box::new([0u8; 0x2000]);
    for chunk in mem.chunks_exact_mut(8) {
        chunk.copy_from_slice(&rng.next().to_le_bytes());
    }
    let mut vpr = [0u128; 32];
    for v in vpr.iter_mut() {
        *v = rng.vector();
    }
    RspState {
        gpr,
        vpr,
        acc: [rng.vector(), rng.vector(), rng.vector()],
        flags: [rng.flag(), rng.flag(), rng.flag(), rng.flag(), rng.flag()],
        div: (rng.next() as i16, rng.next() as i16, rng.below(2) == 0),
        mem,
        decode_imem,
        decode: 0,
        branch: (0, 0),
        broken: false,
    }
}

const VU_NAMES: [&str; 64] = [
    "vmulf", "vmulu", "vrndp", "vmulq", "vmudl", "vmudm", "vmudn", "vmudh", "vmacf", "vmacu",
    "vrndn", "vmacq", "vmadl", "vmadm", "vmadn", "vmadh", "vadd", "vsub", "vsut(18)", "vabs",
    "vaddc", "vsubc", "vaddb(22)", "vsubb(23)", "vaccb(24)", "vsucb(25)", "vsad(26)", "vsac(27)",
    "vsum(28)", "vsar", "vacc(30)", "vsuc(31)", "vlt", "veq", "vne", "vge", "vcl", "vch", "vcr",
    "vmrg", "vand", "vnand", "vor", "vnor", "vxor", "vnxor", "v46", "v47", "vrcp", "vrcpl",
    "vrcph", "vmov", "vrsq", "vrsql", "vrsqh", "vnop", "vextt(56)", "vextq(57)", "vextn(58)",
    "v59", "vinst(60)", "vinsq(61)", "vinsn(62)", "vnull(63)",
];
const LS_NAMES: [&str; 12] = ["bv", "sv", "lv", "dv", "qv", "rv", "pv", "uv", "hv", "fv", "wv", "tv"];
const LS_SCALE: [i32; 12] = [1, 2, 4, 8, 16, 16, 8, 8, 16, 16, 16, 16];

struct Tally {
    rows: Vec<(String, u64, u64, Option<String>)>,
}

impl Tally {
    fn add(&mut self, name: &str, ok: bool, example: impl FnOnce() -> String) {
        let row = match self.rows.iter_mut().position(|r| r.0 == name) {
            Some(i) => &mut self.rows[i],
            None => {
                self.rows.push((name.to_string(), 0, 0, None));
                self.rows.last_mut().unwrap()
            }
        };
        row.1 += 1;
        if !ok {
            row.2 += 1;
            if row.3.is_none() {
                row.3 = Some(example());
            }
        }
    }
}

fn describe(op: u32, pc: u32, input: &RspState, a: &RspState, b: &RspState) -> String {
    let mut s = format!("opcode {op:08x} at SP_PC {pc:#05x}");
    let diffs = |s: &mut String, name: &str, x: &[u128], y: &[u128], inp: &[u128]| {
        for k in 0..x.len() {
            if x[k] != y[k] {
                *s += &format!(
                    "\n      {name}[{k}]: input {:032x}\n        original {:032x}\n        candidate {:032x}",
                    inp[k], x[k], y[k]
                );
            }
        }
    };
    diffs(&mut s, "vpr", &a.vpr, &b.vpr, &input.vpr);
    diffs(&mut s, "acc(l,m,h)", &a.acc, &b.acc, &input.acc);
    diffs(&mut s, "flags(vcol,vcoh,vccl,vcch,vce)", &a.flags, &b.flags, &input.flags);
    for k in 0..32 {
        if a.gpr[k] != b.gpr[k] {
            s += &format!("\n      gpr[{k}]: input {:08x} original {:08x} candidate {:08x}", input.gpr[k], a.gpr[k], b.gpr[k]);
        }
    }
    if a.div != b.div {
        s += &format!("\n      div(in,out,dp): original {:?} candidate {:?}", a.div, b.div);
    }
    if a.mem != b.mem {
        let k = (0..0x2000).find(|&k| a.mem[k] != b.mem[k]).unwrap();
        s += &format!(
            "\n      {} byte {:#06x}: original {:02x} candidate {:02x}",
            if k < 0x1000 { "DMEM" } else { "IMEM" },
            k & 0xFFF,
            a.mem[k],
            b.mem[k]
        );
    }
    if a.decode != b.decode {
        s += "\n      IMEM pre-decode differs";
    }
    if a.branch != b.branch || a.broken != b.broken {
        s += &format!(
            "\n      branch(state,pc)/broken: original {:?}/{} candidate {:?}/{}",
            a.branch, a.broken, b.branch, b.broken
        );
    }
    s
}

pub fn run(trials: u64, seed: u64) -> bool {
    let mut orig = crate::glue_original::Unit::new();
    let mut cand = crate::glue_candidate::Unit::new();
    let mut rng = Rng::new(seed);
    let mut tally = Tally { rows: Vec::new() };
    let mut check = |tally: &mut Tally, name: &str, st: &RspState, op: u32, pc: u32| {
        let a = orig.exec(st, op, pc);
        let b = cand.exec(st, op, pc);
        tally.add(name, a == b, || describe(op, pc, st, &a, &b));
    };

    // vector ALU
    for funct in 0..64u32 {
        for t in 0..trials {
            let st = random_state(&mut rng, false);
            let (mut vd, vs, mut vt) = (rng.below(32) as u32, rng.below(32) as u32, rng.below(32) as u32);
            match t % 8 {
                0 => vd = vs,
                1 => vt = vs,
                2 => {
                    vd = vs;
                    vt = vs
                }
                _ => {}
            }
            let e = if t < 16 { t as u32 } else { rng.below(16) as u32 };
            let op = asm::vu(funct, vd, vs, vt, e);
            check(&mut tally, VU_NAMES[funct as usize], &st, op, 0);
        }
    }

    // vector loads and stores
    for store in [false, true] {
        for kind in 0..12u32 {
            let name = format!("{}{}", if store { "s" } else { "l" }, LS_NAMES[kind as usize]);
            let scale = LS_SCALE[kind as usize];
            let mut one = |tally: &mut Tally, rng: &mut Rng, e: u32, addr: u32| {
                let st0 = random_state(rng, store);
                let mut st = st0;
                let base = 1 + rng.below(31) as u32;
                let off = rng.below(128) as i32 - 64;
                st.gpr[base as usize] = addr.wrapping_sub((off * scale) as u32);
                let vt = rng.below(32) as u32;
                let op = asm::vls(store, kind, vt, e, off, base);
                check(tally, &name, &st, op, 0);
            };
            for e in 0..16 {
                for region in [0x000u32, 0x7F0, 0xFF0, 0xFF8] {
                    for align in 0..16 {
                        one(&mut tally, &mut rng, e, region + align);
                    }
                }
            }
            for _ in 0..trials {
                let e = rng.below(16) as u32;
                let addr = rng.next() as u32;
                one(&mut tally, &mut rng, e, addr);
            }
        }
    }

    // mtc2: every element, every 16-bit value (the upper half random)
    {
        let base = random_state(&mut rng, false);
        for e in 0..16u32 {
            let mut st = base.clone();
            for value in 0..=0xFFFFu32 {
                let rt = 1 + (value % 31);
                let vd = (value >> 5) % 32;
                st.gpr[rt as usize] = value | ((rng.next() as u32) << 16);
                let op = (0x12 << 26) | (4 << 21) | (rt << 16) | (vd << 11) | (e << 7);
                check(&mut tally, "mtc2", &st, op, 0);
            }
        }
    }
    // mfc2, cfc2, ctc2
    for t in 0..trials * 4 {
        let st = random_state(&mut rng, false);
        let e = (t % 16) as u32;
        let rt = rng.below(32) as u32;
        let vd = rng.below(32) as u32;
        check(&mut tally, "mfc2", &st, (0x12 << 26) | (rt << 16) | (vd << 11) | (e << 7), 0);
        let rd = rng.below(4) as u32;
        check(&mut tally, "cfc2", &st, (0x12 << 26) | (2 << 21) | (rt << 16) | (rd << 11), 0);
        check(&mut tally, "ctc2", &st, (0x12 << 26) | (6 << 21) | (rt << 16) | (rd << 11), 0);
    }

    // the scalar unit
    let special: [(u32, &str); 22] = [
        (0, "sll"), (2, "srl"), (3, "sra"), (4, "sllv"), (6, "srlv"), (7, "srav"), (8, "jr"),
        (9, "jalr"), (13, "break"), (32, "add"), (33, "addu"), (34, "sub"), (35, "subu"),
        (36, "and"), (37, "or"), (38, "xor"), (39, "nor"), (42, "slt"), (43, "sltu"), (1, "special1"),
        (5, "special5"), (44, "special44"),
    ];
    let itype: [(u32, &str); 22] = [
        (2, "j"), (3, "jal"), (4, "beq"), (5, "bne"), (6, "blez"), (7, "bgtz"), (8, "addi"),
        (9, "addiu"), (10, "slti"), (11, "sltiu"), (12, "andi"), (13, "ori"), (14, "xori"),
        (15, "lui"), (32, "lb"), (33, "lh"), (35, "lw"), (36, "lbu"), (37, "lhu"), (39, "lwu"),
        (40, "sb"), (41, "sh"),
    ];
    for t in 0..trials {
        let pc = (rng.below(0x400) as u32) * 4;
        let (rs, rt, rd, sa) = (rng.below(32) as u32, rng.below(32) as u32, rng.below(32) as u32, rng.below(32) as u32);
        let (f, name) = special[(t % special.len() as u64) as usize];
        let st = random_state(&mut rng, false);
        check(&mut tally, name, &st, asm::r(f, rs, rt, rd, sa), pc);
        let (op, name) = itype[(t % itype.len() as u64) as usize];
        let st = random_state(&mut rng, op >= 40);
        let imm = rng.next() as u32 & 0xFFFF;
        check(&mut tally, name, &st, if op <= 3 { (op << 26) | (rng.next() as u32 & 0x3FF_FFFF) } else { asm::i(op, rs, rt, imm) }, pc);
        let st = random_state(&mut rng, true);
        check(&mut tally, "sw", &st, asm::i(43, rs, rt, imm), pc);
        let regimm = [(0u32, "bltz"), (1, "bgez"), (16, "bltzal"), (17, "bgezal")][(t % 4) as usize];
        let st = random_state(&mut rng, false);
        check(&mut tally, regimm.1, &st, asm::i(1, rs, regimm.0, imm), pc);
    }

    let mut failed = false;
    let total: u64 = tally.rows.iter().map(|r| r.1).sum();
    for (name, n, bad, example) in &tally.rows {
        if *bad > 0 {
            failed = true;
            println!("rsp-unit: {name}: {bad} of {n} states differ; first:\n    {}", example.as_ref().unwrap());
        }
    }
    if failed {
        println!("rsp-unit: DIVERGENCE (seed {seed})");
    } else {
        println!(
            "rsp-unit: {} instruction kinds, {total} executions: original and candidate identical (seed {seed})",
            tally.rows.len()
        );
    }
    !failed
}
