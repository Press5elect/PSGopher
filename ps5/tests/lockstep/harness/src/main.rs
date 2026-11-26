// PSGopher lockstep harness: Gopher64's interpreters, original against candidate.
// Run through ps5/tests/run-lockstep.sh, which builds both and passes the arguments here.
//
//   lockstep compare (--rom PATH | --synthetic) [--vis N] [--rsp-every K] [run options]
//   lockstep trace --core original|candidate (--rom PATH | --synthetic) --out FILE
//                  [--save-state FILE [--save-state-at VI]] [...]
//   lockstep diff-traces A B
//   lockstep rsp-unit [--trials N] [--seed N]
//   lockstep make-rom OUT
// run options: --state FILE (a Gopher64 savestate, loaded at the first screen update),
//   --overclock, --no-expansion-pak, --seed N (the core's RNG), --rtc SECONDS (the cartridge RTC),
//   --save-types eeprom4k,sram,... (else the ROM header's, else EEPROM 4K + SRAM)
//
// SPDX-License-Identifier: GPL-3.0-or-later
mod asm;
mod rsp_unit;
mod snapshot;
mod synth;

mod glue_original {
    use original as gcore;
    include!("glue.rs");
}
mod glue_candidate {
    use candidate as gcore;
    include!("glue.rs");
}

use snapshot::{Kind, Snapshot};
use std::process::exit;

pub struct RunConfig {
    pub rom: Vec<u8>,
    pub vis: u64,
    pub rsp_every: u64,
    pub overclock: bool,
    pub no_expansion_pak: bool,
    pub seed: u64,
    pub rtc: i64,
    pub state: Option<std::path::PathBuf>,
    pub save_types: Option<Vec<String>>,
    /// write a savestate at this VI checkpoint (trace only)
    pub save_at: Option<(u64, std::path::PathBuf)>,
}

struct Args(Vec<String>);
impl Args {
    fn flag(&mut self, name: &str) -> bool {
        if let Some(i) = self.0.iter().position(|a| a == name) {
            self.0.remove(i);
            true
        } else {
            false
        }
    }
    fn value(&mut self, name: &str) -> Option<String> {
        let i = self.0.iter().position(|a| a == name)?;
        if i + 1 >= self.0.len() {
            die(&format!("{name} needs a value"));
        }
        let v = self.0.remove(i + 1);
        self.0.remove(i);
        Some(v)
    }
    fn num(&mut self, name: &str, default: u64) -> u64 {
        self.value(name).map_or(default, |v| {
            let v = v.replace('_', "");
            if let Some(h) = v.strip_prefix("0x") {
                u64::from_str_radix(h, 16).unwrap_or_else(|_| die(&format!("{name}: not a number")))
            } else {
                v.parse().unwrap_or_else(|_| die(&format!("{name}: not a number")))
            }
        })
    }
    fn done(&self) {
        if !self.0.is_empty() {
            die(&format!("unknown arguments: {}", self.0.join(" ")));
        }
    }
}

fn die(msg: &str) -> ! {
    eprintln!("lockstep: {msg}");
    exit(2)
}

fn load_rom(path: &str) -> Vec<u8> {
    let data = std::fs::read(path).unwrap_or_else(|e| die(&format!("{path}: {e}")));
    if data.len() < 0x1000 {
        die(&format!("{path}: too small for a ROM"));
    }
    // z64 (big-endian), v64 (byte-swapped) or n64 (little-endian), as Gopher64 accepts
    match u32::from_be_bytes(data[0..4].try_into().unwrap()) {
        0x8037_1240 => data,
        0x3780_4012 => data.chunks(2).flat_map(|c| [c[1], c[0]]).collect(),
        0x4012_3780 => data.chunks(4).flat_map(|c| [c[3], c[2], c[1], c[0]]).collect(),
        _ => die(&format!("{path}: not an N64 ROM (z64, v64 or n64)")),
    }
}

fn run_config(a: &mut Args) -> RunConfig {
    let rom = if a.flag("--synthetic") {
        synth::rom()
    } else if let Some(p) = a.value("--rom") {
        load_rom(&p)
    } else {
        die("give --rom PATH or --synthetic")
    };
    RunConfig {
        rom,
        vis: a.num("--vis", 300),
        rsp_every: a.num("--rsp-every", 1),
        overclock: a.flag("--overclock"),
        no_expansion_pak: a.flag("--no-expansion-pak"),
        seed: a.num("--seed", 0x5053_4750),
        rtc: a.num("--rtc", 946_684_800) as i64,
        state: a.value("--state").map(Into::into),
        save_types: a.value("--save-types").map(|s| s.split(',').map(str::to_string).collect()),
        save_at: None,
    }
}

type Runner = fn(&RunConfig, std::sync::mpsc::SyncSender<Snapshot>);

fn spawn(name: &str, f: Runner, cfg: std::sync::Arc<RunConfig>) -> std::sync::mpsc::Receiver<Snapshot> {
    let (tx, rx) = std::sync::mpsc::sync_channel(4096);
    std::thread::Builder::new()
        .name(name.into())
        .stack_size(256 << 20)
        .spawn(move || f(&cfg, tx))
        .unwrap();
    rx
}

fn report(a: &Snapshot, b: &Snapshot, prev: Option<&Snapshot>) {
    println!("lockstep: DIVERGENCE at {}", a.label());
    match prev {
        Some(p) => println!("  last identical checkpoint: {}", p.label()),
        None => println!("  (at the first checkpoint)"),
    }
    println!("  components that differ: {}", snapshot::differing_components(a, b).join(", "));
    for line in snapshot::diff(a, b).iter().take(40) {
        println!("    {line}");
    }
}

fn compare(mut a: Args) {
    let cfg = std::sync::Arc::new(run_config(&mut a));
    let trace_dir = a.value("--trace-dir");
    a.done();
    let start = std::time::Instant::now();
    let ro = spawn("original", glue_original::run, cfg.clone());
    let rc = spawn("candidate", glue_candidate::run, cfg.clone());
    let mut traces = trace_dir.map(|d| {
        std::fs::create_dir_all(&d).unwrap();
        let open = |n: &str| std::io::BufWriter::new(std::fs::File::create(format!("{d}/{n}.trace")).unwrap());
        (open("original"), open("candidate"))
    });
    let (mut vis, mut tasks) = (0u64, 0u64);
    let mut prev: Option<Snapshot> = None;
    loop {
        let (x, y) = (ro.recv(), rc.recv());
        if let Some((to, tc)) = traces.as_mut() {
            use std::io::Write;
            if let Ok(x) = &x {
                writeln!(to, "{}", x.trace_line()).unwrap();
            }
            if let Ok(y) = &y {
                writeln!(tc, "{}", y.trace_line()).unwrap();
            }
        }
        match (x, y) {
            (Ok(x), Ok(y)) => {
                if x != y {
                    if let Some((to, tc)) = traces.as_mut() {
                        use std::io::Write;
                        to.flush().unwrap();
                        tc.flush().unwrap();
                    }
                    report(&x, &y, prev.as_ref());
                    exit(1);
                }
                match x.kind {
                    Kind::Vi => vis += 1,
                    Kind::Rsp => tasks += 1,
                }
                prev = Some(x);
            }
            (Err(_), Err(_)) => break,
            (Ok(x), Err(_)) => {
                println!("lockstep: DIVERGENCE: the candidate stopped (or panicked) before {}", x.label());
                exit(1);
            }
            (Err(_), Ok(y)) => {
                println!("lockstep: DIVERGENCE: the original stopped (or panicked) before the candidate's {}", y.label());
                exit(1);
            }
        }
    }
    if vis < cfg.vis {
        println!("lockstep: both builds stopped after {vis} of {} VIs (a panic? see above)", cfg.vis);
        exit(1);
    }
    let last = prev.unwrap();
    println!(
        "lockstep: no divergence: {vis} VIs, {tasks} RSP-burst checkpoints, COUNT {} at the end, PC {:#x}, {:.1} s",
        last.count,
        last.pc,
        start.elapsed().as_secs_f64()
    );
}

fn trace(mut a: Args) {
    let core = a.value("--core").unwrap_or_else(|| die("--core original|candidate"));
    let out = a.value("--out").unwrap_or_else(|| die("--out FILE"));
    let mut cfg = run_config(&mut a);
    if let Some(p) = a.value("--save-state") {
        cfg.save_at = Some((a.num("--save-state-at", cfg.vis.saturating_sub(1)), p.into()));
    }
    let cfg = std::sync::Arc::new(cfg);
    a.done();
    let f: Runner = match core.as_str() {
        "original" => glue_original::run,
        "candidate" => glue_candidate::run,
        _ => die("--core original|candidate"),
    };
    let rx = spawn(&core, f, cfg);
    use std::io::Write;
    let mut w = std::io::BufWriter::new(std::fs::File::create(&out).unwrap());
    let mut n = 0;
    for s in rx {
        writeln!(w, "{}", s.trace_line()).unwrap();
        n += 1;
    }
    println!("lockstep: {n} checkpoints -> {out}");
}

fn diff_traces(mut a: Args) {
    if a.0.len() != 2 {
        die("diff-traces A B");
    }
    let read = |p: &str| std::fs::read_to_string(p).unwrap_or_else(|e| die(&format!("{p}: {e}")));
    let (ta, tb) = (read(&a.0[0]), read(&a.0[1]));
    a.0.clear();
    let mut last = None;
    for (k, (x, y)) in ta.lines().zip(tb.lines()).enumerate() {
        if x != y {
            println!("lockstep: DIVERGENCE at checkpoint line {}", k + 1);
            if let Some(l) = last {
                println!("  last identical: {l}");
            }
            let fields = |s: &str| s.split_whitespace().map(str::to_string).collect::<Vec<_>>();
            let (fx, fy) = (fields(x), fields(y));
            println!("  first trace:  {}", fx[..3.min(fx.len())].join(" "));
            println!("  second trace: {}", fy[..3.min(fy.len())].join(" "));
            for (p, q) in fx.iter().zip(fy.iter()).skip(3) {
                if p != q {
                    println!("    {p}  vs  {q}");
                }
            }
            exit(1);
        }
        last = Some(x.to_string());
    }
    let (na, nb) = (ta.lines().count(), tb.lines().count());
    if na != nb {
        println!("lockstep: DIVERGENCE: identical for {} checkpoints, then one trace ends ({na} vs {nb})", na.min(nb));
        exit(1);
    }
    println!("lockstep: traces identical ({na} checkpoints)");
}

fn main() {
    let mut argv: Vec<String> = std::env::args().skip(1).collect();
    if argv.is_empty() {
        die("compare | trace | diff-traces | rsp-unit | make-rom (see ps5/tests/run-lockstep.sh)");
    }
    let cmd = argv.remove(0);
    let mut a = Args(argv);
    match cmd.as_str() {
        "compare" => compare(a),
        "trace" => trace(a),
        "diff-traces" => diff_traces(a),
        "rsp-unit" => {
            let trials = a.num("--trials", 4000);
            let seed = a.num("--seed", 1);
            a.done();
            if !rsp_unit::run(trials, seed) {
                exit(1);
            }
        }
        "make-rom" => {
            if a.0.len() != 1 {
                die("make-rom OUT");
            }
            std::fs::write(&a.0[0], synth::rom()).unwrap();
            println!("lockstep: synthetic ROM -> {}", a.0[0]);
        }
        _ => die(&format!("unknown command {cmd}")),
    }
}
