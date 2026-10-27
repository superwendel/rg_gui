#!/usr/bin/env python3
"""Build/verify/measure the public Linux offscreen Dear ImGui comparison.
Dependencies are supplied explicitly; this script never downloads libraries.
All measured frames are retained. See README.md for scope and differences
from the archived private Windows harness.
"""
import argparse,csv,hashlib,json,os,platform,shlex,shutil,statistics,subprocess,sys,time
from validate_captures import compare as compare_captures
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
CASES=['labels_static','labels_dynamic','inspector','list_scroll','mixed_inspector','asset_browser','text_editor','docked_workspace']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
def checked(cmd,**kwargs):
 r=subprocess.run([str(v)for v in cmd],capture_output=True,text=True,**kwargs)
 if r.returncode:raise RuntimeError(f"Command failed ({r.returncode}): {shlex.join([str(v)for v in cmd])}\n{r.stdout}{r.stderr}")
 return r

def build(args):
 dest=args.build_dir.resolve();dest.mkdir(parents=True,exist_ok=True)
 imgui=args.imgui_root.resolve();core=args.core_root.resolve();text=args.text_root.resolve()
 if not(imgui/'backends/imgui_impl_sdlgpu3.cpp').exists():raise RuntimeError('Supply the pinned Dear ImGui docking source root with --imgui-root')
 cflags=shlex.split(checked(['pkg-config','--cflags','sdl3']).stdout);libs=shlex.split(checked(['pkg-config','--libs','sdl3']).stdout)
 common=['-O2','-g','-DNDEBUG','-Wall','-Wextra','-Werror','-I'+str(HERE),'-I'+str(ROOT/'src'),'-isystem',str(core/'src'),'-isystem',str(text/'src')]
 for value in cflags:
  common+=['-isystem',value[2:]]if value.startswith('-I') else[value]
 cppinclude=['-isystem',str(imgui),'-isystem',str(imgui/'backends')]
 units=[('main.c',HERE/'main.c','c'),('adapter_rg.c',HERE/'adapter_rg.c','c'),('adapter_imgui.cpp',HERE/'adapter_imgui.cpp','cpp')]
 units +=[(f,imgui/f,'dep') for f in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp','backends/imgui_impl_sdlgpu3.cpp']]
 commands=[];objects=[]
 for name,source,kind in units:
  obj=dest/(name.replace('/','_')+'.o');objects.append(obj)
  cmd=[args.cc if kind=='c' else args.cxx,'-std=gnu11'if kind=='c'else'-std=c++17',*common,*cppinclude,'-c',str(source),'-o',str(obj)]
  if kind=='dep':cmd=[v for v in cmd if v!='-Werror']
  headers=list(imgui.glob('*.h'))+list((imgui/'backends').glob('*.h'))
  if kind!='dep':headers += list(HERE.glob('*.h'))+list((ROOT/'src').glob('*.h'))+list((core/'src').glob('*.h'))+list((text/'src').glob('*.h'))
  fingerprint={'command':cmd,'inputs':{str(p):digest(p)for p in [source,*headers]}}
  stamp=obj.with_suffix('.inputs.json')
  if obj.exists() and stamp.exists() and json.loads(stamp.read_text())==fingerprint:
   commands.append(cmd);print('Reused '+name,flush=True);continue
  r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True);(dest/(name.replace('/','_')+'.build.log')).write_text(r.stdout+r.stderr)
  commands.append(cmd)
  if r.returncode:raise RuntimeError(f'Build failed: {name}\n{r.stdout}{r.stderr}')
  if any(digest(p)!=value for p,value in fingerprint['inputs'].items()):raise RuntimeError('Source changed during compilation; rebuild')
  save(stamp,fingerprint)
  print('Built '+name,flush=True)
 binary=dest/'compare_gui';cmd=[args.cxx,*map(str,objects),*libs,'-lm','-o',str(binary)];checked(cmd,cwd=ROOT);commands.append(cmd)
 # Matching RG SPIRV is required; it is generated outside measurement.
 r=checked(['bash','build.sh','shaders'],cwd=ROOT);(dest/'shaders.build.log').write_text(r.stdout+r.stderr)
 files=[p for p in HERE.glob('*')if p.suffix in ['.c','.cpp','.h','.py']]
 files +=list((ROOT/'src').glob('*.h'))+list((core/'src').glob('*.h'))+list((text/'src').glob('*.h'))
 files +=[imgui/p for p in ['imgui.cpp','imgui.h','imgui_internal.h','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp','backends/imgui_impl_sdlgpu3.cpp','backends/imgui_impl_sdlgpu3.h','backends/imgui_impl_sdlgpu3_shaders.h']]
 files +=list((ROOT/'shaders/Compiled/SPIRV').glob('*.spv'))+[ROOT/'examples/assets/inter_medium_16.font',ROOT/'examples/assets/inter_medium_16.rgba']
 meta={'binary':str(binary),'executable_sha256':digest(binary),'commands':commands,'compiler':checked([args.cc,'--version']).stdout,'cxx':checked([args.cxx,'--version']).stdout,'sdl':checked(['pkg-config','--modversion','sdl3']).stdout.strip(),'git_revision':checked(['git','rev-parse','HEAD'],cwd=ROOT).stdout.strip(),'tracked_diff':checked(['git','diff','--stat'],cwd=ROOT).stdout,'source_hashes':{str(p):digest(p)for p in files if p.exists()},'imgui_root':str(imgui),'dependency_downloads':json.loads((imgui.parent/'dependency-downloads.json').read_text())if(imgui.parent/'dependency-downloads.json').exists()else None,'created_utc':datetime.now(timezone.utc).isoformat()}
 save(binary.with_suffix('.build.json'),meta);print(binary)

def cpu_pin():
 allowed=sorted(os.sched_getaffinity(0));selected=allowed[0];os.sched_setaffinity(0,{selected})
 return {'selected_cpu':selected,'allowed_before':allowed,'model':next((v.split(':',1)[1].strip() for v in Path('/proc/cpuinfo').read_text().splitlines()if v.startswith('model name')),'unknown')}

def run(args):
 binary=args.binary.resolve();meta_path=binary.with_suffix('.build.json')
 meta=json.loads(meta_path.read_text())
 if meta['executable_sha256']!=digest(binary):raise RuntimeError('Executable does not match build metadata')
 if args.command=='measure':
  proof=json.loads((args.validation/'manifest.json').read_text())
  if proof.get('status')!='complete' or proof['build']['executable_sha256']!=digest(binary) or set(args.case or CASES)-set(proof['cases']):raise RuntimeError('A complete matching verification run is required before timing')
 output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
 snapshot=output/'harness';snapshot.mkdir()
 for p in HERE.iterdir():
  if p.suffix in ['.c','.cpp','.h','.py','.md']:shutil.copy2(p,snapshot/p.name)
 manifest={'status':'incomplete','started_utc':datetime.now(timezone.utc).isoformat(),'platform':platform.platform(),'cpu':cpu_pin(),'build':meta,'configuration':{'target':[1280,800],'backend':'Vulkan','presentation':'none/offscreen','font':'shared prebaked Inter16 bitmaps and advances; kerning disabled','native_shape_antialiasing':False,'rounding':0,'framebuffer_flights':[1,2],'queue':'per-slot fences; one flight waits after submission, two flights throttle before slot reuse','frame_metric':'CPU frame-loop wall time including queue fences; excludes shared fixture generation, initialization, file output, validation; not GPU timestamps or display FPS','ui_metric':'native widget submission and CPU renderer preparation','imgui_branch':'docking for all workloads','editor':'short hard-newline8KiB paragraphs; native soft-wrap enabled; alternating actual text insert/backspace','skip_present_or_tiles':False},'frames':args.frames,'warmup':args.warmup,'rounds':args.rounds,'cases':args.case or CASES,'load_average_start':os.getloadavg(),'runner_sha256':digest(__file__),'environment':{k:os.environ.get(k)for k in ['SDL_VIDEODRIVER','SDL_GPU_DRIVER','DISPLAY','WAYLAND_DISPLAY']}}
 env=dict(os.environ,SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER','x11'),SDL_GPU_DRIVER='vulkan')
 save(output/'manifest.json',manifest);records=[];kind='verify'if args.command=='verify'else'measure'
 for trial in range(args.rounds):
  names=args.case or CASES;rotated=names[trial%len(names):]+names[:trial%len(names)]
  for name in rotated:
   for flights in [1,2]:
    order=['rg','imgui']if trial%2==0 else['imgui','rg']
    for library in order:
     stem=f'{name}_f{flights}_{library}_r{trial+1}';prefix=output/stem
     cmd=[str(binary),'--library',library,'--case',name,'--flights',str(flights),'--frames',str(args.frames),'--warmup',str(args.warmup),'--output',str(prefix)]
     if kind=='verify':cmd+=['--verify']
     start=time.monotonic();r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=args.timeout)
     prefix.with_suffix('.stdout.log').write_text(r.stdout);prefix.with_suffix('.stderr.log').write_text(r.stderr)
     if r.returncode:raise RuntimeError(f'{stem} failed({r.returncode}): {r.stderr}')
     lines=[line for line in r.stdout.splitlines()if line.startswith('{')];result=json.loads(lines[-1]);result.update(trial=trial+1,wall_seconds=time.monotonic()-start,prefix=str(prefix))
     if result['frames']!=args.frames:raise RuntimeError('Incomplete measured frame count')
     with prefix.with_suffix('.frames.csv').open()as f:rows=list(csv.DictReader(f))
     if len(rows)!=args.frames or any(float(v['frame_us'])<0 for v in rows):raise RuntimeError('Invalid frame capture')
     records.append(result);save(output/'results.json',records)
     print(f"{stem}: frame {result['timings_us']['frame']['p50']:.2f}us; UI {result['timings_us']['ui']['p50']:.2f}us",flush=True)
 if kind=='verify':
  pixel_checks=[]
  for name in args.case or CASES:
   for flights in [1,2]:
    row=compare_captures(output/f'{name}_f{flights}_rg_r1.rgba',output/f'{name}_f{flights}_imgui_r1.rgba',name)
    pixel_checks.append(row)
  save(output/'pixel-validation.json',pixel_checks)
  if not all(v['passed']for v in pixel_checks):raise RuntimeError('Missing text in a visible widget or core pixel mismatch; inspect pixel-validation.json')
  # Exact core text parity; native widget pixels intentionally differ.
  comparisons=[]
  for case in [c for c in (args.case or CASES)if c in ['labels_static','labels_dynamic']]:
   for flights in [1,2]:
    left=(output/f'{case}_f{flights}_rg_r1.rgba').read_bytes();right=(output/f'{case}_f{flights}_imgui_r1.rgba').read_bytes()
    if len(left)!=1280*800*4 or len(left)!=len(right):raise RuntimeError('Incomplete readback')
    differences=sum(abs(a-b)>2 for a,b in zip(left,right));comparisons.append({'case':case,'flights':flights,'channels_outside_two_levels':differences})
  save(output/'core-pixel-parity.json',comparisons)
  if any(row['channels_outside_two_levels']for row in comparisons):raise RuntimeError('Core text readbacks differ; inspect captures before timing')
 summary=[]
 for name in args.case or CASES:
  for flights in [1,2]:
   row={'case':name,'flights':flights}
   for library in ['rg','imgui']:
    matches=[v for v in records if v['case']==name and v['flights']==flights and v['library']==library]
    row[library]={metric:{'median_p50':statistics.median(v['timings_us'][metric]['p50']for v in matches),'min_p50':min(v['timings_us'][metric]['p50']for v in matches),'max_p50':max(v['timings_us'][metric]['p50']for v in matches),'median_p95':statistics.median(v['timings_us'][metric]['p95']for v in matches)}for metric in ['frame','ui','upload','encode','submit','wait']}
   row['frame_rg_reduction_percent']=(1-row['rg']['frame']['median_p50']/row['imgui']['frame']['median_p50'])*100
   summary.append(row)
 save(output/'summary.json',summary);manifest.update(status='complete',ended_utc=datetime.now(timezone.utc).isoformat(),process_count=len(records),load_average_end=os.getloadavg());save(output/'manifest.json',manifest)
 lines=['# Linux offscreen GUI comparison','','Times are microseconds; median of process medians. Frame includes CPU/GPU-fence waits, not hardware GPU timestamps.','', '|Case|Flights|rg_gui frame|ImGui frame|rg_gui UI/prep|ImGui UI/prep|','|---|---:|---:|---:|---:|---:|']
 for row in summary:lines.append(f"|{row['case']}|{row['flights']}|{row['rg']['frame']['median_p50']:.2f}|{row['imgui']['frame']['median_p50']:.2f}|{row['rg']['ui']['median_p50']:.2f}|{row['imgui']['ui']['median_p50']:.2f}|")
 (output/'summary.md').write_text('\n'.join(lines)+'\n')

def main():
 parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
 p=sub.add_parser('build');p.add_argument('--imgui-root',type=Path,required=True);p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--cc',default='gcc');p.add_argument('--cxx',default='g++');p.add_argument('--core-root',type=Path,default=ROOT.parent/'rg_core');p.add_argument('--text-root',type=Path,default=ROOT.parent/'rg_text')
 for name in ['verify','measure']:
  p=sub.add_parser(name);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--frames',type=int,default=512 if name=='verify' else 2048);p.add_argument('--warmup',type=int,default=0 if name=='verify' else 512);p.add_argument('--rounds',type=int,default=1 if name=='verify' else 6);p.add_argument('--case',action='append',choices=CASES);p.add_argument('--timeout',type=int,default=180)
  if name=='measure':p.add_argument('--validation',type=Path,required=True,help='Completed verify output for the same binary')
 args=parser.parse_args()
 if args.command=='build':build(args)
 else:
  if args.frames<1 or args.warmup<0 or args.rounds<1:parser.error('Positive frames/rounds and nonnegative warmup required')
  run(args)
if __name__=='__main__':
 try:main()
 except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired)as e:print('comparison error:',e,file=sys.stderr);sys.exit(1)
