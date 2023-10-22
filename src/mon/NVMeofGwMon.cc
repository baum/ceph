/*
 * NVMeGWMonitor.cc
 *
 *  Created on: Oct 17, 2023
 *      Author:
 */


#include <boost/tokenizer.hpp>
#include "include/stringify.h"
#include "NVMeofGwMon.h"

using std::map;
using std::make_pair;
using std::ostream;
using std::ostringstream;


#define dout_subsys ceph_subsys_mon
#undef dout_prefix
#define dout_prefix _prefix(_dout, this, this)
using namespace TOPNSPC::common;

static ostream& _prefix(std::ostream *_dout, const NVMeofGwMon *h,//const Monitor &mon,
        const NVMeofGwMon *hmon) {
    return *_dout << "mon." << hmon->mon.name << "@" << hmon->mon.rank;
}
#define MY_MON_PREFFIX " NVMeGW "


void NVMeofGwMon::init(){
    dout(4) << MY_MON_PREFFIX << __func__  <<  "called " << dendl;
}

void NVMeofGwMon::on_shutdown() {

}
static int cnt ;
void NVMeofGwMon::tick(){

    if (!is_active() || !mon.is_leader()){
        dout(4) << __func__  <<  " NVMeofGwMon leader : " << mon.is_leader() << "active : " << is_active()  << dendl;
        return;
    }

    const auto now = ceph::coarse_mono_clock::now();
    dout(4) << MY_MON_PREFFIX << __func__  <<  "NVMeofGwMon leader got a real tick, pending epoch "<< pending_map.epoch  << dendl;
    last_tick = now;

    if( ++cnt  == 4  ){// simulation that new configuration was added
        pending_map.cfg_add_gw(1, "nqn2008.node1", 1);
        pending_map.cfg_add_gw(2, "nqn2008.node1", 2);
        pending_map.cfg_add_gw(3, "nqn2008.node1", 3);
        pending_map.cfg_add_gw(1, "nqn2008.node2", 2);
        pending_map._dump_gwmap(pending_map.Gmap);

        pending_map.debug_encode_decode();
        dout(4) << "Dump map after decode encode:" <<dendl;
        pending_map._dump_gwmap(pending_map.Gmap);
    }
    else if( cnt  == 20  ){  // simulate - add another GW - to check that new map would be synchronized with peons
        pending_map.cfg_add_gw(2, "nqn2008.node2", 3);
        pending_map._dump_gwmap(pending_map.Gmap);
    }

}


void NVMeofGwMon::create_pending(){

    pending_map = map;// deep copy of the object
    // TODO  since "pending_map"  can be reset  each time during paxos re-election even in the middle of the changes i think static GW configuration
    //should be stored in the "map" .In all other usecases it is immutable
    pending_map.epoch++;
    //map.epoch ++;
    dout(4) <<  MY_MON_PREFFIX << __func__ << " pending epoch " << pending_map.epoch  << dendl;

    dout(5) <<  MY_MON_PREFFIX << "dump_pending"  << dendl;

    pending_map._dump_gwmap(pending_map.Gmap);
}

void NVMeofGwMon::encode_pending(MonitorDBStore::TransactionRef t){
    dout(4) <<  MY_MON_PREFFIX << __func__  << dendl;
    bufferlist bl;
    pending_map.encode(bl);
    put_version(t, pending_map.epoch, bl);
    put_last_committed(t, pending_map.epoch);
}

void NVMeofGwMon::update_from_paxos(bool *need_bootstrap){
    version_t version = get_last_committed();

    dout(4) <<  MY_MON_PREFFIX << __func__ << " version "  << version  << " map.epoch " << map.epoch << dendl;

    if (version != map.epoch) {
        dout(4) << " NVMeGW loading version " << version  << " " << map.epoch << dendl;

        bufferlist bl;
        int err = get_version(version, bl);
        ceph_assert(err == 0);

        auto p = bl.cbegin();
        map.decode(p);
        map._dump_gwmap(map.Gmap);

    }
}



bool NVMeofGwMon::preprocess_query(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}

bool NVMeofGwMon::prepare_update(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}

bool NVMeofGwMon::preprocess_command(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}

bool NVMeofGwMon::prepare_command(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}


bool NVMeofGwMon::preprocess_beacon(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}

bool NVMeofGwMon::prepare_beacon(MonOpRequestRef op){
    dout(4) <<  MY_MON_PREFFIX <<__func__  << dendl;
    return true;
}
