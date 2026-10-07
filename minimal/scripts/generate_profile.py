#!/usr/bin/env python3
"""Generate explicit X4 hardware variants; no default silicon is inferred."""
import argparse
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
IDS={'board_power':1,'i2c':2,'panel':3,'gt911':4,'frontlight':5,'buttons':6,'battery':7,'rtc':8,'sd':9}
PATHS={'board_power':'board','i2c':'i2c','panel':'panel','gt911':'touch','frontlight':'light','buttons':'buttons','battery':'battery','rtc':'rtc','sd':'sd'}
def profile(panel, sleep=False):
    if panel not in ('ssd1677','uc8279'):raise ValueError('Explicit supported panel required')
    uc=panel=='uc8279'
    devices=[]
    def add(name,compatible,kind,config,bindings=None,version=1):
        vendor,model=compatible.split(',',1)
        item={'instance_id':IDS[name],'chip':{'vendor':vendor,'model':model,'revision':'unspecified'},'compatible':compatible,'config_type':kind,'config_version':version,'config':config}
        if bindings:item['bindings']=bindings
        devices.append(item)
    def bank(pins,high,pull):return {'pins':pins,'active_high':high,'pull_up':pull,'debounce_us':0,'long_press_us':0,'click_min_us':0}
    def peripheral(address,irq=-1,high=False):return {'bus_instance_id':100,'address':address,'chip_id':0,'irq':irq,'irq_active_high':high,'irq_pull_up':False}
    add('board_power','xteink,x4-pro-peripheral-enable','gpio.bank',bank([1],True,False))
    add('i2c','espressif,esp32s3-i2c','controller.i2c',{'bus_instance_id':100},{'board.power.ready':1})
    add('panel','ultrachip,uc8279' if uc else 'solomon-systech,ssd1677','display.spi',{'bus_instance_id':101,'width':800,'height':480,'offset_x':0,'offset_y':120 if uc else 0,'rotation':0,'cs':13,'dc':18,'reset':14,'backlight':-1,'busy':6,'reset_active_high':False,'busy_active_high':not uc,'backlight_active_high':False,'power_pins':[],'power_active_high':[],'reset_assert_ms':50 if uc else 10,'reset_recovery_ms':50 if uc else 10},{'board.power.ready':1,'display.frontlight':5})
    add('gt911','goodix,gt911','touch.i2c',{'bus_instance_id':100,'width':480,'height':800,'address':0x5d,'reset_active_high':False,'irq_active_high':False,'irq_pull_up':False,'reset':4,'irq':10,'reset_assert_ms':10,'reset_recovery_ms':10,'power':2,'power_active_high':False,'irq_output':True,'alternate_address':0x14},{'i2c.bus':2,'board.power.ready':1},2)
    add('frontlight','xteink,x4-pro-frontlight','gpio.bank',bank([8,9],True,False))
    buttons=bank([0,7] if sleep else [0,7,3],False,True);buttons['long_press_us']=1000000
    add('buttons','xteink,x4-pro-buttons','gpio.bank',buttons,{'x4.power':17} if sleep else None)
    if sleep:
        devices.append({'instance_id':17,'chip':{'vendor':'xteink','model':'x4-pro-power-key','revision':'unspecified'},'compatible':'xteink,x4-pro-power-key','config_type':'gpio.bank','config_version':1,'config':bank([3],False,True),'bindings':{'board.power.ready':1}})
    add('battery','cellwise,cw2017-readonly-gauge','peripheral.i2c',peripheral(0x63,21,True),{'i2c.bus':2})
    add('rtc','riscrte,pcf8563-compatible-rtc','peripheral.i2c',peripheral(0x51),{'i2c.bus':2})
    add('sd','xteink,x4-pro-sd-native1','gpio.bank',bank([5,41,42,40],True,True),{'board.power.ready':1})
    return {'schema':'riscrte.board-hardware','schema_version':1,'board_id':'xteink-x4-pro','revision':'unspecified','buses':[
        {'instance_id':100,'kind':'i2c','controller_namespace':'esp32.peripheral','controller':0,'frequency_hz':400000,'mode':0,'pins':{'sda':39,'scl':38}},
        {'instance_id':101,'kind':'spi','controller_namespace':'esp32.peripheral','controller':2,'frequency_hz':1000000,'mode':0,'pins':{'sclk':12,'mosi':11,'miso':-1}}], 'devices':devices}
def selections(sleep=False):
    return {**{name:(instance,PATHS[name],'power_buttons' if sleep and name=='buttons' else name) for name,instance in IDS.items()},**({'power':(17,'power','power')} if sleep else {})}
def stage(panel,out,sleep=False):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'board.json').write_text(json.dumps(profile(panel,sleep),indent=2)+'\n')
    drivers=[]
    for name,(instance,folder,provider) in selections(sleep).items():
        manifest=json.loads((ROOT/'minimal/drivers'/('x4pro_'+provider)/'manifest.json').read_text())
        relative=folder+'/manifest.json';path=out/relative;path.parent.mkdir(exist_ok=True);path.write_text(json.dumps(manifest,indent=2)+'\n')
        drivers.append({'manifest':relative,'instance_id':instance})
    (out/'boot.json').write_text(json.dumps({'board':'board.json','default_app':'default.elf','drivers':drivers},indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--panel',choices=['ssd1677','uc8279'],required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sleep',action='store_true');a=p.parse_args();stage(a.panel,a.output,a.sleep)
