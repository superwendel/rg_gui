#!/usr/bin/env python3
"""Compare the full demo's direct and --tile-cache paths in alternating pairs.

Usage: python3 benchmarks/measure_tiles.py <scenario> <vsync|mailbox|immediate> <measured_frames> <pairs> [name=flags ...]

Variants default to direct and --tile-cache; for example
  static vsync 600 3 "direct=" "tiles=--tile-cache" "skip=--tile-cache --tile-cache-skip-present"

For each pair the demo runs twice per variant: warmup frames only, then warmup
plus measured frames. The difference removes startup and warmup. Around each run
the script reads every readable Intel RAPL energy counter
(/sys/class/powercap/intel-rapl:*/energy_uj, normally root-only; package is
system-wide) and the i915 RC6 residency counter
(/sys/class/drm/card*/gt/gt0/rc6_residency_ms). "gpu-busy-ms" is wall time minus
RC6 residency: time the GPU spent outside its deepest idle state, which includes
every GPU client on the system and power-state transitions, not this process's
render pass alone. Treat both as whole-system indicators and compare pairs taken
under the same load; the load average is printed with each pair. Energy is
reported per frame and as average power, because a variant that skips presents
is paced by its own wait rather than the swapchain and so has a different frame
period; power is the fair comparison across variants. A dependable energy claim
needs a quiet machine and several rounds. Run from the
repository root after building out/linux/rg_gui_demo_full. Linux only."""
import subprocess, sys, time, glob, os, statistics
RAPL=[p for p in sorted(glob.glob('/sys/class/powercap/intel-rapl:*/energy_uj')) if os.access(p,os.R_OK)]
NAMES={p: open(p.replace('energy_uj','name')).read().strip() for p in RAPL}
# Each counter wraps at its own documented range, not necessarily 2**32.
WRAP={p: int(open(p.replace('energy_uj','max_energy_range_uj')).read())+1 for p in RAPL}
RC6=sorted(glob.glob('/sys/class/drm/card*/gt/gt0/rc6_residency_ms'))[:1]
DEMO='out/linux/rg_gui_demo_full'; WARMUP=120
def read(p): return int(open(p).read())
def run(scenario, present, frames, flags):
    cmd=[DEMO,'--scenario',scenario,'--present',present,'--frames',str(frames),'--profile-warmup',str(WARMUP)]+flags
    before={p:read(p) for p in RAPL}; rc0=read(RC6[0]) if RC6 else 0; t0=time.time()
    r=subprocess.run(cmd,capture_output=True,text=True,env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_GPU_DRIVER='vulkan'))
    dt=time.time()-t0; after={p:read(p) for p in RAPL}; rc1=read(RC6[0]) if RC6 else 0
    if r.returncode: raise SystemExit(r.stderr[-300:])
    out={NAMES[p]: ((after[p]-before[p]) % WRAP[p])/1e6 for p in RAPL}
    # GPU busy milliseconds over the run: wall time minus time spent in RC6 idle.
    if RC6: out['gpu-busy-ms']=dt*1000.0-(rc1-rc0)
    out['wall-s']=dt
    return out, dt
def main():
    scenario,present,measured,pairs=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
    # Optional variants as name=flags; default compares the direct and tile-cache paths.
    variants={}
    for spec in sys.argv[5:]:
        name,_,flags=spec.partition('=')
        variants[name]=flags.split()
    if not variants: variants={'direct':[], 'tile-cache':['--tile-cache']}
    names=list(variants)
    results={v:{} for v in variants}
    for i in range(pairs):
        order=names[i % len(names):]+names[:i % len(names)]  # rotate who runs first
        for v in order:
            short,_=run(scenario,present,WARMUP,variants[v])
            full,dt=run(scenario,present,WARMUP+measured,variants[v])
            measured_seconds=full['wall-s']-short['wall-s']
            for k in full:
                if k=='wall-s': continue
                scale = 1.0 if k=='gpu-busy-ms' else 1000.0
                results[v].setdefault(k,[]).append(scale*(full[k]-short[k])/measured)
                # Variants that skip presents are not paced by the swapchain, so their
                # frame period differs; power (energy per second) compares durations fairly.
                if k!='gpu-busy-ms' and measured_seconds>0:
                    results[v].setdefault(k+' power-W',[]).append((full[k]-short[k])/measured_seconds)
            results[v].setdefault('frame-period-ms',[]).append(1000.0*measured_seconds/measured)
        print(f"pair {i+1}: load {os.getloadavg()[0]:.2f}  " + "  ".join(f"{v} {k} {results[v][k][-1]:.2f}" for v in order for k in results[v] if k in ('package-0 power-W','gpu-busy-ms','frame-period-ms')), flush=True)
    base=names[0]
    for k in next(iter(results.values())):
        unit='ms GPU busy/frame' if k=='gpu-busy-ms' else 'ms' if k=='frame-period-ms' else 'W' if k.endswith('power-W') else 'mJ/frame'
        print(f"{scenario} {present} {k} ({unit}):")
        for v in names:
            d=results[v][k]
            extra=''
            if v!=base:
                wins=sum(1 for a,b in zip(results[base][k],d) if b<a)
                extra=f"  lower than {base} in {wins}/{len(d)} pairs"
            print(f"  {v:14s} median {statistics.median(d):8.2f} [{min(d):.2f}-{max(d):.2f}]{extra}")
if __name__=='__main__': main()
