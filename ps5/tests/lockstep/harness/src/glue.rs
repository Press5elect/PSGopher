// Included once per build (`use original as gcore` / `use candidate as gcore`): the same code reads
// both builds' state, so a field the candidate renames or retypes stops this from compiling
// rather than being compared wrongly.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
use crate::rsp_unit::RspState;
use crate::snapshot::{Hasher, Kind, RDRAM_BLOCK, Snapshot, hash_bytes};
use gcore::device::Device;
use gcore::lockstep_hook::Point;
use std::arch::x86_64::__m128i;

fn m(x: __m128i) -> u128 {
    unsafe { std::mem::transmute(x) }
}
fn to_m(x: u128) -> __m128i {
    unsafe { std::mem::transmute(x) }
}

pub fn snapshot(d: &Device, kind: Kind, vi: u64, task: u64) -> Snapshot {
    let c = &d.cpu;
    let r = &d.rsp.cpu;
    let mut tlb = Hasher::new();
    for e in &c.cop0.tlb_entries {
        for v in [
            e.mask, e.vpn2, e.region as u64, e.g as u64, e.asid as u64, e.pfn_even, e.c_even as u64,
            e.d_even as u64, e.v_even as u64, e.pfn_odd, e.c_odd as u64, e.d_odd as u64,
            e.v_odd as u64, e.start_even, e.end_even, e.phys_even, e.start_odd, e.end_odd,
            e.phys_odd,
        ] {
            tlb.u64(v);
        }
    }
    let mut caches = Hasher::new();
    for l in &d.memory.icache {
        caches.u64(l.valid as u64).u64(l.tag as u64).u64(l.index as u64);
        for w in l.words {
            caches.u64(w as u64);
        }
    }
    for l in &d.memory.dcache {
        caches.u64(l.valid as u64).u64(l.dirty as u64).u64(l.tag as u64).u64(l.index as u64);
        for w in l.words {
            caches.u64(w as u64);
        }
    }
    let mut decode = Hasher::new();
    for ins in &r.instructions {
        decode.u64(ins.opcode as u64);
    }
    let rdram = if kind == Kind::Vi {
        d.rdram.mem.chunks(RDRAM_BLOCK).map(hash_bytes).collect()
    } else {
        Vec::new()
    };
    Snapshot {
        kind,
        vi,
        task,
        count: c.cop0.regs[gcore::device::cop0::COP0_COUNT_REG],
        pc: c.pc,
        gpr: c.gpr,
        hi: c.hi,
        lo: c.lo,
        llbit: c.llbit,
        branch: (c.branch_state.state.clone() as u8, c.branch_state.pc),
        cop0: c.cop0.regs,
        fcr31: c.cop1.fcr31,
        fgr: c.cop1.fgr,
        tlb: tlb.finish(),
        caches: caches.finish(),
        events: {
            // Gopher64's own 16 events; the 64DD's (16-20, PSGopher's) must stay off in a cartridge game
            assert!(c.events.iter().skip(16).all(|e| !e.enabled), "a 64DD event is on");
            c.events.iter().take(16).map(|e| (e.enabled, e.count)).collect()
        },
        next_event: (c.next_event_count, c.next_event),
        rsp_gpr: r.gpr,
        vpr: r.vpr.map(m),
        acc: [m(r.accl), m(r.accm), m(r.acch)],
        flags: [m(r.vcol), m(r.vcoh), m(r.vccl), m(r.vcch), m(r.vce)],
        div: (r.divin, r.divout, r.divdp),
        sp_regs: d.rsp.regs.to_vec(),
        sp_pc: d.rsp.regs2[gcore::device::rsp_interface::SP_PC_REG],
        rsp_misc: (
            r.cycle_counter,
            r.broken,
            r.pipeline_full,
            r.branch_state.state.clone() as u8,
            r.branch_state.pc,
        ),
        dmem: hash_bytes(&d.rsp.mem[..0x1000]),
        imem: hash_bytes(&d.rsp.mem[0x1000..]),
        imem_decode: decode.finish(),
        rcp: vec![
            ("mi", d.mi.regs.to_vec()),
            ("vi", d.vi.regs.to_vec()),
            ("ai", d.ai.regs.to_vec()),
            ("pi", d.pi.regs.to_vec()),
            ("si", d.si.regs.to_vec()),
            ("ri", d.ri.regs.to_vec()),
            ("dpc", d.rdp.regs_dpc.to_vec()),
            ("dps", d.rdp.regs_dps.to_vec()),
            ("rdram.regs", d.rdram.regs.iter().flatten().copied().collect()),
        ],
        pif_ram: hash_bytes(&d.pif.ram),
        rdram,
    }
}

struct Run {
    tx: std::sync::mpsc::SyncSender<Snapshot>,
    vis: u64,
    rsp_every: u64,
    vi: u64,
    task: u64,
    save_at: Option<(u64, std::path::PathBuf)>,
}

thread_local! {
    static RUN: std::cell::RefCell<Option<Run>> = const { std::cell::RefCell::new(None) };
}

fn hook(d: &mut Device, point: Point) {
    RUN.with(|run| {
        let mut run = run.borrow_mut();
        let run = run.as_mut().unwrap();
        if run.vi >= run.vis {
            d.cpu.running = false;
            return;
        }
        match point {
            Point::Vi => {
                let s = snapshot(d, Kind::Vi, run.vi, run.task);
                if let Some((at, path)) = &run.save_at
                    && *at == run.vi
                {
                    gcore::write_state(d, path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
                    eprintln!("lockstep: savestate at VI {at} -> {}", path.display());
                }
                if std::env::var_os("LOCKSTEP_DEBUG").is_some() {
                    eprintln!(
                        "lockstep: [{}] VI {} PC {:#x} COUNT {} RSP bursts {} SP_STATUS {:#x} Status {:#x} Cause {:#x} MI {:x?}",
                        std::thread::current().name().unwrap_or("?"),
                        run.vi,
                        d.cpu.pc,
                        s.count,
                        run.task,
                        d.rsp.regs[gcore::device::rsp_interface::SP_STATUS_REG],
                        d.cpu.cop0.regs[12],
                        d.cpu.cop0.regs[13],
                        d.mi.regs
                    );
                }
                run.vi += 1;
                if run.tx.send(s).is_err() || run.vi >= run.vis {
                    d.cpu.running = false;
                }
            }
            Point::RspTask => {
                run.task += 1;
                if run.rsp_every > 0 && run.task % run.rsp_every == 0 {
                    let s = snapshot(d, Kind::Rsp, run.vi, run.task);
                    if run.tx.send(s).is_err() {
                        d.cpu.running = false;
                    }
                }
            }
        }
    })
}

pub fn run(cfg: &crate::RunConfig, tx: std::sync::mpsc::SyncSender<Snapshot>) {
    RUN.with(|r| {
        *r.borrow_mut() = Some(Run { tx, vis: cfg.vis, rsp_every: cfg.rsp_every, vi: 0, task: 0, save_at: cfg.save_at.clone() })
    });
    let save_types = cfg.save_types.as_ref().map(|names| {
        names
            .iter()
            .map(|n| {
                gcore::ui::storage::parse_save_type(n)
                    .unwrap_or_else(|| panic!("unknown save type {n}"))
            })
            .collect()
    });
    gcore::run(
        gcore::Boot {
            rom: &cfg.rom,
            overclock: cfg.overclock,
            disable_expansion_pak: cfg.no_expansion_pak,
            rng_seed: cfg.seed,
            rtc_timestamp: cfg.rtc,
            state: cfg.state.clone(),
            save_types,
        },
        hook,
    );
    RUN.with(|r| *r.borrow_mut() = None);
}

/// One RSP instruction on a given state, through the build's own decoder and handler.
pub struct Unit {
    d: Box<Device>,
}

impl Unit {
    pub fn new() -> Unit {
        Unit { d: gcore::unit_device() }
    }

    pub fn exec(&mut self, st: &RspState, opcode: u32, pc: u32) -> RspState {
        let d = &mut self.d;
        let r = &mut d.rsp.cpu;
        r.gpr = st.gpr;
        r.vpr = st.vpr.map(to_m);
        r.accl = to_m(st.acc[0]);
        r.accm = to_m(st.acc[1]);
        r.acch = to_m(st.acc[2]);
        r.vcol = to_m(st.flags[0]);
        r.vcoh = to_m(st.flags[1]);
        r.vccl = to_m(st.flags[2]);
        r.vcch = to_m(st.flags[3]);
        r.vce = to_m(st.flags[4]);
        r.divin = st.div.0;
        r.divout = st.div.1;
        r.divdp = st.div.2;
        r.branch_state.state = gcore::device::cpu::State::Step;
        r.branch_state.pc = 0;
        r.broken = false;
        d.rsp.mem.copy_from_slice(&st.mem[..]);
        if st.decode_imem {
            for k in 0..0x400 {
                let a = 0x1000 + 4 * k;
                let w = u32::from_be_bytes(d.rsp.mem[a..a + 4].try_into().unwrap());
                d.rsp.cpu.instructions[k] = gcore::device::rsp_cpu::decode_opcode(d, w);
            }
        }
        d.rsp.regs2[gcore::device::rsp_interface::SP_PC_REG] = pc;
        let ins = gcore::device::rsp_cpu::decode_opcode(d, opcode);
        d.rsp.cpu.instruction_type = ins.instruction_type;
        (ins.func)(d, opcode);
        let r = &d.rsp.cpu;
        let mut decode = Hasher::new();
        if st.decode_imem {
            for ins in &r.instructions {
                decode.u64(ins.opcode as u64);
            }
        }
        let mut mem = Box::new([0u8; 0x2000]);
        mem.copy_from_slice(&d.rsp.mem);
        RspState {
            gpr: r.gpr,
            vpr: r.vpr.map(m),
            acc: [m(r.accl), m(r.accm), m(r.acch)],
            flags: [m(r.vcol), m(r.vcoh), m(r.vccl), m(r.vcch), m(r.vce)],
            div: (r.divin, r.divout, r.divdp),
            mem,
            decode_imem: st.decode_imem,
            decode: decode.finish(),
            branch: (r.branch_state.state.clone() as u8, r.branch_state.pc),
            broken: r.broken,
        }
    }
}
