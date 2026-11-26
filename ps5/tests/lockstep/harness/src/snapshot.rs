// A checkpoint of the emulated machine, the same shape for both builds, and how two compare.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Vi,
    Rsp,
}

#[derive(Clone, PartialEq)]
pub struct Snapshot {
    pub kind: Kind,
    /// screen updates before this one, RSP bursts before this one
    pub vi: u64,
    pub task: u64,
    // VR4300
    pub count: u64,
    pub pc: u64,
    pub gpr: [u64; 32],
    pub hi: u64,
    pub lo: u64,
    pub llbit: bool,
    pub branch: (u8, u64),
    pub cop0: [u64; 32],
    pub fcr31: u32,
    pub fgr: [u64; 32],
    pub tlb: u64,
    pub caches: u64,
    pub events: Vec<(bool, u64)>,
    pub next_event: (u64, usize),
    // RSP
    pub rsp_gpr: [u32; 32],
    pub vpr: [u128; 32],
    /// accl, accm, acch
    pub acc: [u128; 3],
    /// vcol, vcoh, vccl, vcch, vce
    pub flags: [u128; 5],
    /// divin, divout, divdp
    pub div: (i16, i16, bool),
    pub sp_regs: Vec<u32>,
    pub sp_pc: u32,
    /// cycle_counter, broken, pipeline_full, branch state, branch pc
    pub rsp_misc: (u64, bool, bool, u8, u32),
    pub dmem: u64,
    pub imem: u64,
    /// hash of the pre-decoded IMEM opcodes (must follow IMEM)
    pub imem_decode: u64,
    // the rest of the machine
    pub rcp: Vec<(&'static str, Vec<u32>)>,
    pub pif_ram: u64,
    /// RDRAM in 64 KiB blocks (VI checkpoints only)
    pub rdram: Vec<u64>,
}

pub const RDRAM_BLOCK: usize = 0x10000;

pub struct Hasher(u64);
impl Hasher {
    pub fn new() -> Hasher {
        Hasher(0xCBF2_9CE4_8422_2325)
    }
    pub fn u64(&mut self, v: u64) -> &mut Self {
        self.0 = (self.0 ^ v).wrapping_mul(0x9E37_79B9_7F4A_7C15).rotate_left(31);
        self
    }
    pub fn bytes(&mut self, b: &[u8]) -> &mut Self {
        let mut chunks = b.chunks_exact(8);
        for c in &mut chunks {
            self.u64(u64::from_le_bytes(c.try_into().unwrap()));
        }
        for &x in chunks.remainder() {
            self.u64(x as u64 | 0x100);
        }
        self.u64(b.len() as u64)
    }
    pub fn finish(&self) -> u64 {
        let mut x = self.0;
        x ^= x >> 33;
        x = x.wrapping_mul(0xFF51_AFD7_ED55_8CCD);
        x ^ (x >> 33)
    }
}

pub fn hash_bytes(b: &[u8]) -> u64 {
    Hasher::new().bytes(b).finish()
}
fn hash_u64s(v: impl IntoIterator<Item = u64>) -> u64 {
    let mut h = Hasher::new();
    for x in v {
        h.u64(x);
    }
    h.finish()
}
fn hash_u128s(v: &[u128]) -> u64 {
    hash_u64s(v.iter().flat_map(|x| [*x as u64, (*x >> 64) as u64]))
}

const COP0_NAMES: [&str; 32] = [
    "Index", "Random", "EntryLo0", "EntryLo1", "Context", "PageMask", "Wired", "cop0_7",
    "BadVAddr", "Count", "EntryHi", "Compare", "Status", "Cause", "EPC", "PRId", "Config",
    "LLAddr", "WatchLo", "WatchHi", "XContext", "cop0_21", "cop0_22", "cop0_23", "cop0_24",
    "cop0_25", "ParityError", "CacheError", "TagLo", "TagHi", "ErrorEPC", "cop0_31",
];

impl Snapshot {
    pub fn label(&self) -> String {
        match self.kind {
            Kind::Vi => format!("VI {} (after RSP burst {}, COUNT {})", self.vi, self.task, self.count),
            Kind::Rsp => format!("RSP burst {} (in VI {}, COUNT {})", self.task, self.vi, self.count),
        }
    }

    /// One hash per component, in a fixed order (the trace file's columns).
    pub fn components(&self) -> Vec<(&'static str, u64)> {
        let mut rcp = Hasher::new();
        for (_, regs) in &self.rcp {
            for r in regs {
                rcp.u64(*r as u64);
            }
        }
        let mut v = vec![
            ("count", self.count),
            ("cpu", {
                let mut h = Hasher::new();
                h.u64(self.pc).u64(self.hi).u64(self.lo).u64(self.llbit as u64);
                h.u64(self.branch.0 as u64).u64(self.branch.1);
                for g in self.gpr {
                    h.u64(g);
                }
                h.finish()
            }),
            ("cop0", hash_u64s(self.cop0)),
            ("cop1", hash_u64s(self.fgr.iter().copied().chain([self.fcr31 as u64]))),
            ("tlb", self.tlb),
            ("caches", self.caches),
            (
                "events",
                hash_u64s(
                    self.events
                        .iter()
                        .flat_map(|e| [e.0 as u64, e.1])
                        .chain([self.next_event.0, self.next_event.1 as u64]),
                ),
            ),
            ("rsp", {
                let mut h = Hasher::new();
                for g in self.rsp_gpr {
                    h.u64(g as u64);
                }
                for r in &self.sp_regs {
                    h.u64(*r as u64);
                }
                h.u64(self.sp_pc as u64).u64(self.rsp_misc.0).u64(self.rsp_misc.1 as u64);
                h.u64(self.rsp_misc.2 as u64).u64(self.rsp_misc.3 as u64).u64(self.rsp_misc.4 as u64);
                h.finish()
            }),
            ("vu", {
                let mut all: Vec<u128> = self.vpr.to_vec();
                all.extend(self.acc);
                all.extend(self.flags);
                all.push(((self.div.0 as u16 as u128) << 32) | ((self.div.1 as u16 as u128) << 16) | self.div.2 as u128);
                hash_u128s(&all)
            }),
            ("dmem", self.dmem),
            ("imem", self.imem ^ self.imem_decode.rotate_left(1)),
            ("rcp", rcp.finish()),
            ("pif", self.pif_ram),
        ];
        if self.kind == Kind::Vi {
            v.push(("rdram", hash_u64s(self.rdram.iter().copied())));
        }
        v
    }

    pub fn trace_line(&self) -> String {
        let mut s = format!(
            "{} vi={} task={}",
            if self.kind == Kind::Vi { "vi" } else { "rsp" },
            self.vi,
            self.task
        );
        for (name, h) in self.components() {
            if name == "count" {
                s += &format!(" count={h}");
            } else {
                s += &format!(" {name}={h:016x}");
            }
        }
        s
    }
}

fn diff_array<T: PartialEq + std::fmt::Debug>(
    out: &mut Vec<String>,
    name: &str,
    names: Option<&[&str]>,
    a: &[T],
    b: &[T],
    hex: bool,
) {
    for (k, (x, y)) in a.iter().zip(b.iter()).enumerate() {
        if x != y {
            let n = names.map_or(format!("{name}[{k}]"), |n| format!("{name}.{}", n[k]));
            if hex {
                out.push(format!("{n}: original {x:x?} candidate {y:x?}"));
            } else {
                out.push(format!("{n}: original {x:?} candidate {y:?}"));
            }
        }
    }
    if a.len() != b.len() {
        out.push(format!("{name}: length {} vs {}", a.len(), b.len()));
    }
}

/// Every difference, most specific first, as lines for the report.
pub fn diff(a: &Snapshot, b: &Snapshot) -> Vec<String> {
    let mut d = Vec::new();
    if a.kind != b.kind || a.vi != b.vi || a.task != b.task {
        d.push(format!(
            "checkpoint order: original at {}, candidate at {} (the RSP ran a different number of bursts)",
            a.label(),
            b.label()
        ));
    }
    let scalar = |d: &mut Vec<String>, n: &str, x: String, y: String| {
        if x != y {
            d.push(format!("{n}: original {x} candidate {y}"));
        }
    };
    scalar(&mut d, "cpu.pc", format!("{:#x}", a.pc), format!("{:#x}", b.pc));
    diff_array(&mut d, "cpu.gpr", None, &a.gpr, &b.gpr, true);
    scalar(&mut d, "cpu.hi", format!("{:#x}", a.hi), format!("{:#x}", b.hi));
    scalar(&mut d, "cpu.lo", format!("{:#x}", a.lo), format!("{:#x}", b.lo));
    scalar(&mut d, "cpu.llbit", a.llbit.to_string(), b.llbit.to_string());
    scalar(&mut d, "cpu.branch(state,pc)", format!("{:x?}", a.branch), format!("{:x?}", b.branch));
    diff_array(&mut d, "cop0", Some(&COP0_NAMES), &a.cop0, &b.cop0, true);
    scalar(&mut d, "cop1.fcr31", format!("{:#x}", a.fcr31), format!("{:#x}", b.fcr31));
    diff_array(&mut d, "cop1.fgr", None, &a.fgr, &b.fgr, true);
    scalar(&mut d, "tlb", format!("{:016x}", a.tlb), format!("{:016x}", b.tlb));
    scalar(&mut d, "caches (icache+dcache lines)", format!("{:016x}", a.caches), format!("{:016x}", b.caches));
    diff_array(&mut d, "events(enabled,count)", None, &a.events, &b.events, false);
    scalar(&mut d, "next_event(count,type)", format!("{:?}", a.next_event), format!("{:?}", b.next_event));
    diff_array(&mut d, "rsp.gpr", None, &a.rsp_gpr, &b.rsp_gpr, true);
    diff_array(&mut d, "rsp.vpr", None, &a.vpr, &b.vpr, true);
    diff_array(&mut d, "rsp.acc", Some(&["accl", "accm", "acch"]), &a.acc, &b.acc, true);
    diff_array(&mut d, "rsp.flags", Some(&["vcol", "vcoh", "vccl", "vcch", "vce"]), &a.flags, &b.flags, true);
    scalar(&mut d, "rsp.div(divin,divout,divdp)", format!("{:?}", a.div), format!("{:?}", b.div));
    diff_array(&mut d, "sp.regs", None, &a.sp_regs, &b.sp_regs, true);
    scalar(&mut d, "sp.pc", format!("{:#x}", a.sp_pc), format!("{:#x}", b.sp_pc));
    scalar(
        &mut d,
        "rsp(cycle_counter,broken,pipeline_full,branch,branch_pc)",
        format!("{:?}", a.rsp_misc),
        format!("{:?}", b.rsp_misc),
    );
    scalar(&mut d, "dmem", format!("{:016x}", a.dmem), format!("{:016x}", b.dmem));
    scalar(&mut d, "imem", format!("{:016x}", a.imem), format!("{:016x}", b.imem));
    scalar(&mut d, "imem pre-decode", format!("{:016x}", a.imem_decode), format!("{:016x}", b.imem_decode));
    for ((n, x), (_, y)) in a.rcp.iter().zip(b.rcp.iter()) {
        diff_array(&mut d, n, None, x, y, true);
    }
    scalar(&mut d, "pif.ram", format!("{:016x}", a.pif_ram), format!("{:016x}", b.pif_ram));
    for (k, (x, y)) in a.rdram.iter().zip(b.rdram.iter()).enumerate() {
        if x != y {
            d.push(format!(
                "rdram {:#08x}..{:#08x} differs",
                k * RDRAM_BLOCK,
                (k + 1) * RDRAM_BLOCK
            ));
        }
    }
    if a.rdram.len() != b.rdram.len() {
        d.push(format!("rdram size {} vs {} blocks", a.rdram.len(), b.rdram.len()));
    }
    d
}

/// The components that differ, for the summary line.
pub fn differing_components(a: &Snapshot, b: &Snapshot) -> Vec<&'static str> {
    a.components()
        .into_iter()
        .zip(b.components())
        .filter(|(x, y)| x.1 != y.1)
        .map(|(x, _)| x.0)
        .collect()
}
