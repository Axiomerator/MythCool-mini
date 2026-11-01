"""Read-only Windows process resource sampler. Python standard library only."""
# SPDX-License-Identifier: MPL-2.0
import argparse,ctypes as c,datetime,json,os,statistics,time
from ctypes import wintypes as w
from pathlib import Path
class Memory(c.Structure):
    _fields_=[('cb',w.DWORD),('faults',w.DWORD)]+[(n,c.c_size_t) for n in ('peak_working_set','working_set','quota_peak_paged','quota_paged','quota_peak_nonpaged','quota_nonpaged','pagefile','peak_pagefile','private_commit','private_working_set')]+[('shared_commit',c.c_uint64)]
class Io(c.Structure):
    _fields_=[(n,c.c_uint64) for n in ('read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes')]
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
k.GetProcessTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
k.K32GetProcessMemoryInfo.argtypes=[w.HANDLE,w.LPVOID,w.DWORD]
k.GetProcessIoCounters.argtypes=[w.HANDLE,c.POINTER(Io)]
k.GetProcessHandleCount.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
def checked(ok):
    if not ok:raise c.WinError(c.get_last_error())
def ft(t):return (t.dwHighDateTime<<32)|t.dwLowDateTime

def sample(handles):
    result=dict(cpu_seconds=0.,working_set=0,private_commit=0,private_working_set=0,handles=0,read_bytes=0,write_bytes=0,other_bytes=0)
    for h in handles:
        created,exited,kernel,user=(w.FILETIME() for _ in range(4))
        checked(k.GetProcessTimes(h,c.byref(created),c.byref(exited),c.byref(kernel),c.byref(user)))
        m=Memory();m.cb=c.sizeof(m);checked(k.K32GetProcessMemoryInfo(h,c.byref(m),c.sizeof(m)))
        io=Io();checked(k.GetProcessIoCounters(h,c.byref(io)));count=w.DWORD();checked(k.GetProcessHandleCount(h,c.byref(count)))
        result['cpu_seconds']+=(ft(kernel)+ft(user))/1e7
        for field in ('working_set','private_commit','private_working_set'):result[field]+=getattr(m,field)
        result['handles']+=count.value
        for field in ('read_bytes','write_bytes','other_bytes'):result[field]+=getattr(io,field)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,action='append',required=True);p.add_argument('--seconds',type=int,default=60);p.add_argument('--output',type=Path,required=True);p.add_argument('--label',default='mini');a=p.parse_args()
    if not 2<=a.seconds<=3600:p.error('--seconds must be 2..3600')
    handles=[]
    try:
        for pid in a.pid:
            h=k.OpenProcess(0x1000,False,pid);checked(h);handles.append(h)
        first=sample(handles);start=time.monotonic();rows=[dict(second=0.,**first)]
        for i in range(1,a.seconds+1):
            time.sleep(max(0,start+i-time.monotonic()));rows.append(dict(second=time.monotonic()-start,**sample(handles)))
        end=rows[-1];elapsed=end['second'];logical=os.cpu_count();cpu_delta=end['cpu_seconds']-first['cpu_seconds']
        summary={'label':a.label,'captured_at':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(timespec='seconds'),'duration_seconds':elapsed,'sample_count':len(rows),'logical_processors':logical,'process_count':len(handles),'cpu_seconds':cpu_delta,'cpu_percent_all_processors':100*cpu_delta/elapsed/logical,'memory_bytes':{key:{'mean':statistics.mean(r[key] for r in rows),'min':min(r[key] for r in rows),'max':max(r[key] for r in rows)} for key in ('working_set','private_commit','private_working_set')},'mean_handle_count':statistics.mean(r['handles'] for r in rows),'process_io_bytes_per_second':{key:(end[key]-first[key])/elapsed for key in ('read_bytes','write_bytes','other_bytes')},'io_note':'Process I/O includes device/USB traffic; these are not disk-only transfer rates.'}
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'summary':summary,'pids':a.pid,'samples':rows},indent=2)+'\n',encoding='utf-8');print(json.dumps(summary,indent=2))
    finally:
        for h in handles:k.CloseHandle(h)
if __name__=='__main__':main()
