"""Admission-time custody of the explicitly selected X4 automatic Light profile."""
import hashlib
from pathlib import Path
HEADERS=('RiscDisplayOutputV1.h','RiscDisplayOutputPowerV1.h','RiscTouchV1.h','RiscTouchPowerV1.h','RiscStorageVolumeV1.h')
RUNTIME_HEADERS=('RiscLightSleepV1.h','RiscTimedSleepV1.h','RiscDeepSleepV1.h')
INSTANCES={'x4.power':17,'display.output':3,'input.touch.raw':4,'storage.volume':9,'net.wifi':15,'bluetooth.hci':16}
def sdk_headers(drivers,runtime):
 return {name:hashlib.sha256(((Path(drivers)/'sdk' if name in HEADERS else Path(runtime)/'sdk/driver')/name).read_bytes()).hexdigest() for name in (*HEADERS,*RUNTIME_HEADERS)}
def validate_app(name,manifest,receipt,helper,headers):
 policy=receipt.get('idle_policy',{})
 expected={'enabled':True,'mode':'Light only; foreground state retained','low_battery_threshold_percent':10,
  'idle_default_ms':60000,'low_battery_idle_ms':20000,'foreground_capture_restart':False,
  'helper_sha256':hashlib.sha256(helper).hexdigest(),'sdk_sha256':headers,'physical_instances':INSTANCES,
  'preferences_instance':1,'alarm_service':{'api':2,'instance':0}}
 if any(policy.get(k)!=v for k,v in expected.items()):raise ValueError('Idle helper/policy/SDK custody mismatch: '+name)
 flags=set(receipt.get('build_defines',[]))
 if not {'-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_LOW_BATTERY','-DPORTABLE_APP_SLEEP_LOCAL','-DPORTABLE_QUICK_RADIOS','-DALARM_SERVICE_TAGGED_V2'}<=flags:
  raise ValueError('Incomplete idle compilation profile: '+name)
 if name=='waterfall' and '-DPORTABLE_RADIO_CONTINUOUS_CAPTURE' not in flags:raise ValueError('RF capture must inhibit automatic idle')
 pairs={(r['capability'],r['api']) for r in manifest['requires']}
 if not ({(c,1) for c in INSTANCES}|{('alarm.service',2),('storage.key-value',1)})<=pairs:raise ValueError('Missing typed idle declaration: '+name)
 return dict(policy)
