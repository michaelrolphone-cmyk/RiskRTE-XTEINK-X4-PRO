/* Exercise the production driver through its public tables and strict scoped
 * I2C/GPIO doubles. Records use the Goodix 0x814f + 8*n wire format. */
#define main gt911_legacy_fixture_main
#include "gt911_test.c"
#undef main

static uint64_t begin(void) {
    driver=t5_driver_get(2);assert(driver);api=driver->capability;
    power=risc_touch_power(api);assert(power && start());
    uint64_t sub=api->subscribe(NULL);assert(sub);return sub;
}
static void two(uint8_t a,uint16_t ax,uint16_t ay,uint8_t b,uint16_t bx,uint16_t by,bool home) {
    packet(2,ax,ay,home);wire_point(0,a,ax,ay);wire_point(1,b,bx,by);
}
static void event(uint64_t sub,uint8_t kind,uint8_t id,uint16_t x,uint16_t y,uint64_t seq) {
    risc_touch_event_v1 e={0};assert(api->next(NULL,sub,&e)==1);
    assert(e.kind==kind && e.id==id && e.x==x && e.y==y && e.sequence==seq && e.timestamp_ms==time_ms);
}
static void contact(const risc_touch_snapshot_v1 *s,unsigned slot,uint8_t id,uint16_t x,uint16_t y) {
    assert(slot<s->contact_count);
    assert(s->contacts[slot].id==id && s->contacts[slot].x==x && s->contacts[slot].y==y && !s->contacts[slot].reserved);
}
static void no_stale_slots(const risc_touch_snapshot_v1 *s) {
    for(unsigned i=s->contact_count;i<RISC_TOUCH_MAX_CONTACTS;++i)
        assert(!s->contacts[i].id && !s->contacts[i].x && !s->contacts[i].y && !s->contacts[i].reserved);
}
static void wire_layout(void) {
    const uint64_t sub=begin();
    /* Literal wire capture: ID 0 at (272,564), ID 7 at (479,799).
     * Distinct size/reserved bytes must not leak into IDs or coordinates. */
    const uint8_t bytes[]={0,0x10,1,0x34,2,0xa5,0x5a,0xff,
                          7,0xdf,1,0x1f,3,0x22,0x11,0xee};
    status=0x82;memcpy(raw,bytes,sizeof(bytes));++time_ms;
    assert(api->poll(NULL,1) && !status);
    event(sub,RISC_TOUCH_EVENT_DOWN,1,272,564,1);
    event(sub,RISC_TOUCH_EVENT_DOWN,8,479,799,2);no_event(sub,0);
    const risc_touch_snapshot_v1 s=snapshot();
    assert(s.contact_count==2 && s.sequence==2);
    contact(&s,0,1,272,564);contact(&s,1,8,479,799);no_stale_slots(&s);
    assert(strstr(trace,"d814e;d814f;a;"));done(sub);
}
static void reorder_and_lift(void) {
    const uint64_t sub=begin();
    two(7,300,500,0,10,20,false);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_DOWN,1,10,20,1);event(sub,RISC_TOUCH_EVENT_DOWN,8,300,500,2);
    two(0,10,20,7,300,500,false);assert(api->poll(NULL,1));no_event(sub,0);
    risc_touch_snapshot_v1 s=snapshot();assert(s.sequence==2);
    contact(&s,0,1,10,20);contact(&s,1,8,300,500);
    two(7,310,510,0,11,21,true);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_MOVE,1,11,21,3);event(sub,RISC_TOUCH_EVENT_MOVE,8,310,510,4);
    event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,0,5);
    /* Lift the lower-ID finger; the other keeps its ID and position. */
    packet(1,0,0,true);wire_point(0,7,310,510);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_UP,1,11,21,6);no_event(sub,0);
    s=snapshot();assert(s.contact_count==1);contact(&s,0,8,310,510);no_stale_slots(&s);
    /* Replacing a finger at identical coordinates is UP then DOWN, no MOVE. */
    packet(1,0,0,true);wire_point(0,15,310,510);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_UP,8,310,510,7);event(sub,RISC_TOUCH_EVENT_DOWN,16,310,510,8);
    packet(0,0,0,false);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_UP,16,310,510,9);event(sub,RISC_TOUCH_EVENT_BUTTON_UP,0,0,0,10);
    s=snapshot();assert(!s.contact_count && !s.buttons);no_stale_slots(&s);done(sub);
}
static void five_contacts(void) {
    const uint64_t sub=begin();
    packet(5,0,0,false);
    for(unsigned i=0;i<5;++i)wire_point(i,(uint8_t)(4-i),(uint16_t)(40+4-i),(uint16_t)(80+4-i));
    assert(api->poll(NULL,1));
    for(unsigned i=0;i<5;++i)event(sub,RISC_TOUCH_EVENT_DOWN,(uint8_t)(i+1),(uint16_t)(40+i),(uint16_t)(80+i),i+1);
    risc_touch_snapshot_v1 s=snapshot();assert(s.contact_count==5);
    for(unsigned i=0;i<5;++i)contact(&s,i,(uint8_t)(i+1),(uint16_t)(40+i),(uint16_t)(80+i));
    packet(5,0,0,true);
    for(unsigned i=0;i<5;++i)wire_point(i,(uint8_t)(15-i),(uint16_t)(200+15-i),(uint16_t)(400+15-i));
    assert(api->poll(NULL,1));
    for(unsigned i=0;i<5;++i)event(sub,RISC_TOUCH_EVENT_UP,(uint8_t)(i+1),(uint16_t)(40+i),(uint16_t)(80+i),i+6);
    for(unsigned i=0;i<5;++i)event(sub,RISC_TOUCH_EVENT_DOWN,(uint8_t)(i+12),(uint16_t)(211+i),(uint16_t)(411+i),i+11);
    event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,0,16);no_event(sub,0);done(sub);
}
static void malformed(void) {
    const uint64_t sub=begin(),other=api->subscribe(NULL);assert(other);
    for(unsigned bad=0;bad<10;++bad) {
        two(0,10,20,7,300,500,true);assert(api->poll(NULL,1));
        const uint64_t seq=snapshot().sequence;
        /* Keep old events unread: rejecting the entire packet must clear both
         * consumers' queues, with no valid-prefix MOVE or Home release. */
        two(0,11,21,7,301,501,false);
        switch(bad) {
        case 0: status=0x86;break;
        case 1: status=0x8f;break;
        case 2: raw[8]=0;break;
        case 3: raw[8]=16;break;
        case 4: raw[8]=32;break;
        case 5: raw[8]=128;break;
        case 6: raw[8]=255;break;
        case 7: wire_point(1,7,480,501);break;
        case 8: wire_point(1,7,301,800);break;
        default: wire_point(1,7,65535,65535);break;
        }
        assert(!api->poll(NULL,1) && !status);
        const risc_touch_snapshot_v1 s=snapshot();assert(!s.contact_count && !s.buttons && s.sequence==seq+1);
        no_event(sub,-1);no_event(sub,0);no_event(other,-1);no_event(other,0);
    }
    assert(api->unsubscribe(NULL,other));done(sub);
}
static void partial_reads(void) {
    const uint64_t sub=begin();
    two(0,10,20,7,300,500,false);assert(api->poll(NULL,1));
    const risc_touch_snapshot_v1 before=snapshot();
    two(0,11,21,7,301,501,true);point_ok=false;
    for(partial_point_bytes=0;partial_point_bytes<16;++partial_point_bytes) {
        assert(!api->poll(NULL,1) && status==0x92);
        const risc_touch_snapshot_v1 after=snapshot();assert(!memcmp(&before,&after,sizeof(before)));
    }
    /* Original DOWN events survive every short read and no ACK is attempted. */
    --time_ms;event(sub,RISC_TOUCH_EVENT_DOWN,1,10,20,1);event(sub,RISC_TOUCH_EVENT_DOWN,8,300,500,2);++time_ms;
    no_event(sub,0);point_ok=true;partial_point_bytes=0;assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_MOVE,1,11,21,3);event(sub,RISC_TOUCH_EVENT_MOVE,8,301,501,4);
    event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,0,5);no_event(sub,0);done(sub);
}
static void acknowledge(void) {
    const uint64_t sub=begin();
    two(0,10,20,7,300,500,true);ack_ok=false;assert(!api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_DOWN,1,10,20,1);event(sub,RISC_TOUCH_EVENT_DOWN,8,300,500,2);
    event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,0,3);
    /* Repeated uncertain ACK, even with record reorder, produces no edges. */
    two(7,300,500,0,10,20,true);assert(!api->poll(NULL,1));no_event(sub,0);
    ack_ok=true;assert(api->poll(NULL,1));no_event(sub,0);assert(snapshot().sequence==3);
    packet(0,0,0,false);ack_ok=false;ack_reaches=true;assert(!api->poll(NULL,1) && !status);
    event(sub,RISC_TOUCH_EVENT_UP,1,10,20,4);event(sub,RISC_TOUCH_EVENT_UP,8,300,500,5);
    event(sub,RISC_TOUCH_EVENT_BUTTON_UP,0,0,0,6);
    ack_ok=true;assert(api->poll(NULL,1));no_event(sub,0);assert(snapshot().sequence==6);done(sub);
}
static void overflow_and_subscribers(void) {
    const uint64_t slow=begin(),fast=api->subscribe(NULL);assert(fast);
    two(0,10,20,7,300,500,false);assert(api->poll(NULL,1));
    event(fast,RISC_TOUCH_EVENT_DOWN,1,10,20,1);event(fast,RISC_TOUCH_EVENT_DOWN,8,300,500,2);
    /* Leave 31 events pending on slow, then a two-event report overflows it. */
    for(unsigned i=0;i<29;++i) {
        two(0,(uint16_t)(11+i),20,7,300,500,false);assert(api->poll(NULL,1));
        event(fast,RISC_TOUCH_EVENT_MOVE,1,(uint16_t)(11+i),20,i+3);
    }
    two(7,301,501,0,40,20,false);assert(api->poll(NULL,1));
    event(fast,RISC_TOUCH_EVENT_MOVE,1,40,20,32);event(fast,RISC_TOUCH_EVENT_MOVE,8,301,501,33);
    no_event(slow,-1);no_event(slow,0);
    const risc_touch_snapshot_v1 s=snapshot();assert(s.contact_count==2 && s.sequence==33);
    contact(&s,0,1,40,20);contact(&s,1,8,301,501);
    assert(api->unsubscribe(NULL,slow));no_event(slow,-1);
    const uint64_t fresh=api->subscribe(NULL);assert(fresh>fast);no_event(fresh,0);
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY && !driver->quiesce());
    packet(0,0,0,false);assert(api->poll(NULL,1));
    for(unsigned i=0;i<2;++i) {
        const uint64_t token=i?fresh:fast;
        event(token,RISC_TOUCH_EVENT_UP,1,40,20,34);event(token,RISC_TOUCH_EVENT_UP,8,301,501,35);no_event(token,0);
    }
    assert(api->unsubscribe(NULL,fast));done(fresh);
}
static void power_neutral(void) {
    const uint64_t old=begin();two(0,10,20,7,300,500,true);assert(api->poll(NULL,1));
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);
    assert(api->unsubscribe(NULL,old));prepared();recovered();
    const uint64_t sub=api->subscribe(NULL);assert(sub>old);no_event(old,-1);
    const uint64_t seq=snapshot().sequence;
    two(7,310,510,0,20,30,true);assert(api->poll(NULL,1));no_event(sub,0);
    risc_touch_snapshot_v1 s=snapshot();assert(!s.contact_count && !s.buttons && s.sequence==seq);no_stale_slots(&s);
    packet(0,0,0,false);ack_ok=false;ack_reaches=true;assert(!api->poll(NULL,1));ack_ok=true;
    two(0,30,40,7,320,520,false);assert(api->poll(NULL,1));no_event(sub,0);
    packet(0,0,0,false);assert(api->poll(NULL,1));no_event(sub,0);
    two(7,330,530,0,40,50,false);assert(api->poll(NULL,1));
    event(sub,RISC_TOUCH_EVENT_DOWN,1,40,50,seq+1);event(sub,RISC_TOUCH_EVENT_DOWN,8,330,530,seq+2);
    done(sub);
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"wire-layout"))wire_layout();
    else if(!strcmp(argv[1],"reorder-lift"))reorder_and_lift();
    else if(!strcmp(argv[1],"five-contacts"))five_contacts();
    else if(!strcmp(argv[1],"malformed"))malformed();
    else if(!strcmp(argv[1],"partial-reads"))partial_reads();
    else if(!strcmp(argv[1],"acknowledge"))acknowledge();
    else if(!strcmp(argv[1],"overflow-subscribers"))overflow_and_subscribers();
    else if(!strcmp(argv[1],"power-neutral"))power_neutral();
    else assert(!"unknown multi-contact scenario");
    printf("X4 GT911 multi-contact PASS: %s\n",argv[1]);return 0;
}
