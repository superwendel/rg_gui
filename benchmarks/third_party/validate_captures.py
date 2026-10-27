"""Independent readback validation for reconstructed GUI comparison.

Only core label cases require cross-library pixels to match. Native widget
shapes legitimately differ; row/panel ink coverage supplements state oracles.
This script does no rendering or timing. Run before benchmark measurements.
"""
import argparse
import hashlib
import json
from pathlib import Path

WIDTH, HEIGHT = 1280, 800

def read_rgba(path):
    data = path.read_bytes()
    if len(data) != WIDTH * HEIGHT * 4:
        raise ValueError(f'{path}: expected {WIDTH*HEIGHT*4} bytes, got {len(data)}')
    return data

def white_ink(data, rect):
    x, y, w, h = rect
    count = 0
    for row in range(max(0,y), min(HEIGHT,y+h)):
        for col in range(max(0,x), min(WIDTH,x+w)):
            i = (row*WIDTH+col)*4
            r,g,b = data[i:i+3]
            if min(r,g,b) >= 190 and max(r,g,b)-min(r,g,b) <= 5:
                count += 1
    return count

def regions(case):
    if case in ('static','dynamic','labels_static','labels_dynamic'):
        return [(f'label-{i}',(16+310*(i//32),12+22*(i%32),300,20)) for i in range(128)]
    if case in ('list','list_scroll'):
        return [(f'row-{i}',(16,32+22*i,600,20)) for i in range(32)]
    if case in ('browser','asset_browser'):
        return [(f'row-{i}',(16,48+22*i,600,20)) for i in range(32)]
    if case in ('inspector','mixed','mixed_inspector'):
        return [(f'checkbox-label-{i}',(32,16+22*i,260,20)) for i in range(32)]
    if case in ('docks','docked_workspace'):
        # Panel bodies, excluding each title/tab bar and central splitter.
        # Require ink well below the title to reject empty-but-framed panes.
        return [(f'panel-body-{i}',(16+640*(i%2),80+400*(i//2),600,290)) for i in range(4)]
    if case in ('editor','text_editor'):
        return [(f'editor-visible-text-{i}',(32,48+16*i,1000,16)) for i in range(20)]
    raise ValueError(f'unknown case: {case}')

def dock_columns(data):
    """Find16distinct text lines in each of8panel columns, excluding tab bars."""
    records=[]
    for panel in range(4):
        px,py=640*(panel%2),400*(panel//2)
        for col in range(2):
            groups=[];start=None;last=None
            for y in range(py+24,min(py+395,HEIGHT)):
                ink=white_ink(data,(px+4+310*col,y,300,1))
                if ink>=8:
                    if start is None:start=y
                    last=y
                elif start is not None:
                    groups.append([start,last]);start=None
            if start is not None:groups.append([start,last])
            records.append({'panel':panel,'column':col,'text_line_spans':groups,
                'all16_labels_visible':len(groups)==16 and all(end-start+1>=8 for start,end in groups)})
    return records

def inspect(path, case):
    data=read_rgba(path)
    counts=[{'region':name,'white_ink_pixels':white_ink(data,rect)} for name,rect in regions(case)]
    result={'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),
            'regions':counts,'all_regions_have_ink':all(row['white_ink_pixels']>=5 for row in counts)}
    if case in ('docks','docked_workspace'):
        result['dock_columns']=dock_columns(data)
        result['all128_dock_labels_visible']=all(row['all16_labels_visible']for row in result['dock_columns'])
    return data,result

def compare(left, right, case):
    a,ar=inspect(left,case);b,br=inspect(right,case)
    result={'case':case,'left':ar,'right':br,'pixel_tolerance':2}
    if case in ('static','dynamic','labels_static','labels_dynamic'):
        channel_bad=sum(abs(x-y)>2 for x,y in zip(a,b))
        pixel_bad=sum(any(abs(a[i+j]-b[i+j])>2 for j in range(4)) for i in range(0,len(a),4))
        result.update(mismatched_channels=channel_bad,mismatched_pixels=pixel_bad,
                      max_channel_difference=max(abs(x-y) for x,y in zip(a,b)),
                      core_pixel_match=channel_bad==0)
    result['passed']=(ar['all_regions_have_ink'] and br['all_regions_have_ink']
                      and ar.get('all128_dock_labels_visible',True)
                      and br.get('all128_dock_labels_visible',True)
                      and result.get('core_pixel_match',True))
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--left',required=True,type=Path)
    parser.add_argument('--right',required=True,type=Path)
    parser.add_argument('--case',required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=compare(args.left,args.right,args.case)
    payload=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(payload)
    print(payload,end='')
    raise SystemExit(0 if result['passed'] else 1)
