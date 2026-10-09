"""Explicit dedicated USB transfer admission; never expands Clock authority."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PROFILE=json.loads((ROOT/'minimal/apps/usb-transfer.json').read_text())
REQUIREMENTS={(g['capability'],g['api']) for g in PROFILE['grants']}
FLAGS={'-DPORTABLE_USB_TRANSFER_APP','-DPORTABLE_APP_LAUNCH_GUARD',
       '-DPORTABLE_APP_OWNS_TOUCH_CHROME','-DPORTABLE_INPUT_NAVIGATION',
       '-DPORTABLE_DISPLAY_ROTATION=90','-DPORTABLE_HOME_APP="default.elf"',
       '-DPORTABLE_RETURN_APP="springboard.elf"'}
def validate(manifest,blob,record,source,msc_header):
    if manifest.get('id')!=PROFILE['id'] or manifest.get('file_name')!=PROFILE['file_name'] or manifest.get('version')!=PROFILE['version']:
        raise ValueError('USB transfer requires its exact dedicated app identity')
    requirements=manifest.get('requires',[])
    if len(requirements)!=4 or any(type(r.get('api')) is not int for r in requirements) or {(r.get('capability'),r.get('api')) for r in requirements}!=REQUIREMENTS:
        raise ValueError('USB transfer requires exactly its four dedicated capabilities')
    if record.get('repository_commit')!=source or record.get('working_tree_dirty') is not False:
        raise ValueError('USB transfer requires clean pinned source custody')
    if record.get('version')!=manifest['version'] or record.get('sha256')!=hashlib.sha256(blob).hexdigest() or record.get('size_bytes')!=len(blob):
        raise ValueError('USB transfer ELF differs from its exact receipt')
    flags=record.get('defines',[])
    if len(flags)!=len(FLAGS) or set(flags)!=FLAGS:
        raise ValueError('USB transfer requires guarded awake foreground ownership')
    if record.get('sdk_sha256')!={'RiscUsbDeviceMscV1.h':hashlib.sha256(msc_header).hexdigest()}:
        raise ValueError('USB transfer SDK contract differs from selected provider')
    for name in ('usb_role','poll_interval_ms','sleep','navigation_requires_release','configured_stop_requires_cable_confirmation'):
        if type(record.get(name)) is not type(PROFILE[name]) or record[name]!=PROFILE[name]:
            raise ValueError('USB transfer ownership policy differs: '+name)
    if record.get('capability')!={'name':'usb.device.msc','api':1,'instance_id':0}:
        raise ValueError('USB transfer must select the unique MSC provider')
    return [dict(g) for g in PROFILE['grants']]
