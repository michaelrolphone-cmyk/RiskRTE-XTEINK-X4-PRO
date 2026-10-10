#!/usr/bin/env python3
"""Production HID + shared UI + real X4 GT911 over physical I/O doubles."""
import argparse,json,os,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--utilities',type=Path,required=True)
p.add_argument('--system',type=Path,required=True)
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--driver',type=Path)
p.add_argument('--output-dir',type=Path)
p.add_argument('--scene',action='append')
a=p.parse_args();root=Path(__file__).resolve().parents[2]
out=(a.output_dir or root/'build/hid-gt911').resolve()
source=a.utilities.resolve()/'scripts/test_hid_renderer.py';body=source.read_text()
fixture=a.utilities.resolve()/'test/native_apps/hid_renderer_test.c'
text=fixture.read_text()
rejection='if(getenv("HID_RENDER_CLEANUP")&&++unsub_attempts==1)return false;'
assert text.count(rejection)==1;text=text.replace(rejection,'')
anchor='static bool fake_unsub(void*c,uint64_t n){(void)c;assert(n>0&&n<5&&subscribers[n].live&&subs);'
assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\n'+rejection)
# A refused test unsubscribe must leave the real provider token owned.
patched=out/'hid_renderer_test.c';patched.parent.mkdir(parents=True,exist_ok=True);patched.write_text(text)
original="ROOT/'test/native_apps/hid_renderer_test.c'"
assert original in body;body=body.replace(original,'Path('+repr(str(patched))+')')

old="extra=['-I'+str(watch/'sdk/driver'),'-I'+str(watch/'include')]"
assert old in body;body=body.replace(old,"extra=['-I'+"+repr(str(a.sdk.resolve()))+"]")
old="ROOT/'test/native_apps/hid_watch_touch_backend.c'"
assert old in body;body=body.replace(old,"Path("+repr(str(root/'minimal/test/hid_gt911_backend.c'))+")")
# Put this integration's output in the product build directory, preserving
# every app/controller assertion in the canonical runner.
old="out=ROOT/'build'/('hid-watch-renderer' if watch else 'hid-paper-renderer' if a.paper else 'hid-renderer')/str(int(san))/name"
assert old in body;body=body.replace(old,"out=Path("+repr(str(out))+")/str(int(san))/name")
sys.path.insert(0,str(source.parent))
sys.argv=[str(source),'--paper','--system-apps',str(a.system.resolve()),'--watch',str(root),'--watch-touch-source',str((a.driver or root/'minimal/drivers/x4pro_gt911/driver.c').resolve())]
for scene in a.scene or ['reconnect','left-tap','tap-drag','scroll-x','scroll-y','scroll-free','keys','pair-accept','pair-reject','pair-move','home','back','cleanup']:
 sys.argv+=['--scene',scene]
exec(compile(body,str(source),'exec'),{'__file__':str(source),'__name__':'__main__'})
