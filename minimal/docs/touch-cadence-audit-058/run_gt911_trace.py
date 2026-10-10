#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess,os
root=pathlib.Path('/workspace/shared/x4-gt911-down-before-move-20261010')
out=pathlib.Path(__file__).parent
sdk=pathlib.Path('/workspace/shared/gt911-contact-order-proof/ci-reconciliation/ordinary-target/sdk')
text=(root/'minimal/test/gt911_test.c').read_text()
text=text.replace('#include "../drivers/x4pro_board_power/PowerReadyV1.h"','#include "'+str(root/'minimal/drivers/x4pro_board_power/PowerReadyV1.h')+'"')
text=text.replace('static size_t trace_size;','''static size_t trace_size;
static unsigned audit_clocks, audit_events, audit_snapshots;
static uint64_t audit_hash=1469598103934665603ULL;
static void audit(const char *format,...) {
 char b[256]; va_list a;va_start(a,format);int n=vsnprintf(b,sizeof(b),format,a);va_end(a);
 assert(n>=0 && (size_t)n<sizeof(b));
 for(int i=0;i<n;++i){audit_hash^=(uint8_t)b[i];audit_hash*=1099511628211ULL;}
}
''')
text=text.replace('++transacts;','++transacts;audit("I%zu,%zu,%u;",tn,rn,timeout);')
text=text.replace('static uint64_t now(void *context) {','static uint64_t now(void *context) { ++audit_clocks;')
text=text.replace('const risc_touch_snapshot_v1 actual=snapshot();','''const risc_touch_snapshot_v1 actual=snapshot();++audit_snapshots;
 audit("S%llu,%llu,%u,%u;",(unsigned long long)actual.sequence,(unsigned long long)actual.timestamp_ms,actual.contact_count,actual.buttons);
 for(unsigned z=0;z<RISC_TOUCH_MAX_CONTACTS;++z)audit("C%u,%u,%u;",actual.contacts[z].id,actual.contacts[z].x,actual.contacts[z].y);''')
text=text.replace('const risc_touch_event_v1 *expected=&reference_events[i];','''++audit_events;audit("E%llu,%llu,%u,%u,%u,%u;",(unsigned long long)actual.sequence,(unsigned long long)actual.timestamp_ms,actual.kind,actual.id,actual.x,actual.y);
        const risc_touch_event_v1 *expected=&reference_events[i];''')
text=text.replace('printf("X4 ordinary GT911 PASS: %s\\n",argv[1]);','''printf("X4 ordinary GT911 PASS: %s\\n",argv[1]);
 printf("audit_hash=%016llx transacts=%u locks=%u operations=%u clocks=%u snapshots=%u events=%u modeled_ms=%llu\\n",(unsigned long long)audit_hash,transacts,takes,operations,audit_clocks,audit_snapshots,audit_events,(unsigned long long)time_ms);''')
fixture=out/'instrumented_single_contact.c';fixture.write_text(text)
old_dir=out/'baseline/minimal/drivers/x4pro_gt911';old_dir.mkdir(parents=True,exist_ok=True)
old=old_dir/'driver.c';old.write_bytes(subprocess.check_output(['git','-C',str(root),'show','89884c5f839dd35d65d7f96742a11371ddcd22f8:minimal/drivers/x4pro_gt911/driver.c']))
h=old_dir.parent/'x4pro_board_power/PowerReadyV1.h';h.parent.mkdir(exist_ok=True);h.write_bytes((root/'minimal/drivers/x4pro_board_power/PowerReadyV1.h').read_bytes())
new=pathlib.Path('/workspace/shared/x4-latest-composition-058/minimal/drivers/x4pro_gt911/driver.c')
results=[]
for sanitize in [False,True]:
 outputs=[]
 for name,source in [('019',old),('0110',new)]:
  exe=out/(name+('-san' if sanitize else ''))
  cmd=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(sdk),str(fixture),str(source),'-o',str(exe)]
  if sanitize:cmd += ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(cmd,check=True)
  env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}
  result=subprocess.run([str(exe),'source-equivalence'],env=env,check=True,capture_output=True,text=True)
  (out/(exe.name+'.log')).write_text(result.stdout+result.stderr)
  outputs.append(result.stdout)
  results.append({'version':name,'sanitize':sanitize,'driver_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'command':cmd,'output':result.stdout})
 assert outputs[0]==outputs[1],outputs
receipt={'scope':'Unchanged 20,000-report single-contact reference test with read-only trace instrumentation; includes valid movement, taps, no READY, invalid counts/coordinates, I2C faults and subscription changes. No physical CPU latency claim.','identical_old_new':True,'runs':results}
(out/'gt911-trace-qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
