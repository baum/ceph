
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

static ostream& _prefix(std::ostream *_dout, const NVMeofGwMap *h,//const Monitor &mon,
        const NVMeofGwMap *map) {
    return *_dout << "mon." << map->mon->name << "@" << map->mon->rank;
}

int  NVMeofGwMap::cfg_add_gw (uint16_t gw_id, const std::string & nqn, uint16_t ana_grpid) {
    GW_STATE_T state{ {GW_IDLE_STATE,} , ana_grpid, GW_AVAILABILITY_E::GW_CREATED, gw_id, 0 };

    if (find_gw_map(gw_id, nqn)) {
        dout(4) << __func__ << " ERROR :GW already exists in map " << gw_id << dendl;
        return 1;
    }
    if (ana_grpid >= MAX_SUPPORTED_ANA_GROUPS && ana_grpid != REDUNDANT_GW_ANA_GROUP_ID)
    {
        dout(4) << __func__ << " ERROR :GW " << gw_id << " bad ANA group " <<(int)ana_grpid << dendl;
        return 1;
    }

    //TODO check that all MAX_SUPPORTED_ANA_GROUPS are occupied in the subsystem - assert

    if(Gmap[nqn].size() ==0 )
        Gmap.insert(make_pair(nqn, std::map<uint16_t, GW_STATE_T>()));
    Gmap[nqn].insert({gw_id, state});
    //epoch++;
    return 0;
}



int NVMeofGwMap::_dump_gwmap(GWMAP & Gmap)const  {

    dout(4) << __func__  <<  " called  " << mon << dendl;
    std::ostringstream ss;
    ss  << std::endl;
    for (auto& itr : Gmap) {
        for (auto& ptr : itr.second) {

            ss	<< " NQN " << itr.first << " GW_ID " << ptr.first << " ANA gr " << std::setw(5) << (int)ptr.second.optimized_ana_group_id << " available " << (int)ptr.second.availability << " States: ";
            for (int i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++) {
                ss << (int)ptr.second.sm_state[i] << " " ;
            }
            ss  << std::endl;
        }
    }
    dout(10) << ss.str() <<dendl;
    return 0;
}



int NVMeofGwMap::process_gw_map_ka(uint16_t gw_id, const std::string& nqn , bool &need_map_update)
{
    int rc = 0;

#define     FAILBACK_PERSISTENCY_INT_SEC 8
    GW_STATE_T* gw_state = find_gw_map(gw_id, nqn);
    if (gw_state) {
        auto subsyst_it = find_subsystem_map(nqn);
        //assert(it)
        dout(4)  << "KA beacon from the GW " << gw_id << " in state " << (int)gw_state->availability << dendl;

        if (gw_state->availability == GW_AVAILABILITY_E::GW_CREATED) {
            // first time appears - allow IO traffic for this GW
            gw_state->availability = GW_AVAILABILITY_E::GW_AVAILABLE;
            for (int i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++) gw_state->sm_state[i] = GW_STANDBY_STATE;
            if (gw_state->optimized_ana_group_id != REDUNDANT_GW_ANA_GROUP_ID) { // not a redundand GW
                //gw_state->ana_state[gw_state->optimized_ana_group_id] = true;
                gw_state->sm_state[gw_state->optimized_ana_group_id] = GW_ACTIVE_STATE;
            }
            publish_map_to_gws(nqn);
        }


        else if (gw_state->availability == GW_AVAILABILITY_E::GW_UNAVAILABLE) {
            gw_state->availability = GW_AVAILABILITY_E::GW_AVAILABLE;
            if (gw_state->optimized_ana_group_id == REDUNDANT_GW_ANA_GROUP_ID) {
                for (int i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++) gw_state->sm_state[i] = GW_STANDBY_STATE;
                publish_map_to_gws(nqn);
            }
            else {// prepare to Failback to this GW
                // find the GW that took over on the group gw_state->optimized_ana_group_id
                bool found = false;
                for (auto& itr : *subsyst_it) {
                    //cout << "Found GW " << itr.second.gw_id << endl;
                    if (itr.second.sm_state[gw_state->optimized_ana_group_id] == GW_ACTIVE_STATE) {
                        dout(4)  << "Found GW " << itr.second.gw_id << " that took over the ANAGRP " << (int)gw_state->optimized_ana_group_id << " of the available GW " << gw_state->gw_id << dendl;
                        itr.second.sm_state[gw_state->optimized_ana_group_id] = GW_WAIT_FAILBACK_PREPARED;
                        add_timestamp_to_metadata(itr.second.gw_id, nqn, gw_state->optimized_ana_group_id);// Add timestamp of start Failback preparation to metadata of gw
                        gw_state->sm_state[gw_state->optimized_ana_group_id]  = GW_BLOCKED_AGROUP_OWNER;
                        publish_map_to_gws(nqn);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    dout(4)  << "Warning - not found the GW responsible for" << gw_state->optimized_ana_group_id << "that took over the GW" << gw_state->gw_id << "when it was fallen" << dendl;
                    //TODO the gw_state->optimized_ana_group_id] was not still open for traffic, seems need just open it
                    gw_state->sm_state[gw_state->optimized_ana_group_id]  = GW_ACTIVE_STATE;
                    publish_map_to_gws(nqn);
                }
            }
        }


        else if (gw_state->availability == GW_AVAILABILITY_E::GW_AVAILABLE) {
            const auto now = ceph::coarse_mono_clock::now(); // const auto mgr_beacon_grace =   g_conf().get_val<std::chrono::seconds>("mon_mgr_beacon_grace");// todo change to something related to NVMeGW KATO
            std::chrono::seconds sc(FAILBACK_PERSISTENCY_INT_SEC);

            for (int i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++)
                if (gw_state->sm_state[i] == GW_WAIT_FAILBACK_PREPARED) {
                    GW_METADATA_T* metadata = find_gw_metadata(gw_id, nqn);
                    //  inspect (now - metadata->anagrp_sm_tstamps[i])
                    if(now - metadata->anagrp_sm_tstamps[i] > sc){ //mgr_beacon_grace){
                   //     interval = 2*KATO pased T  so find  the state of the candidate to failback - whether it is still available
                        for (auto& itr : *subsyst_it) {
                           if (itr.second.sm_state[i] == GW_BLOCKED_AGROUP_OWNER && itr.second.availability == GW_AVAILABILITY_E::GW_AVAILABLE) {
                               gw_state->sm_state[i]  = GW_STANDBY_STATE;
                               itr.second.sm_state[i] = GW_ACTIVE_STATE;
                               dout(4)  << "Failback from GW " << gw_state->gw_id << " to " << itr.second.gw_id << dendl;
                               publish_map_to_gws(nqn);
                               break;
                        }
                      }
                    }
                    // maybe there are other ANA groups that this GW is in state GW_WAIT_FAILBACK_PREPARED  so continue pass over all ANA groups
                }
        }
    }
    else{
        dout(4)  << __FUNCTION__ << "ERROR GW-id was not found in the map " << gw_id << dendl;
        rc = 1;
    }
    return rc;
}



int  NVMeofGwMap::set_failover_gw_for_ANA_group(uint16_t gw_id, const std::string& nqn, uint8_t ANA_groupid)
{
    GW_STATE_T* gw_state = find_gw_map(gw_id, nqn);
    gw_state->sm_state[ANA_groupid] = GW_ACTIVE_STATE;
    publish_map_to_gws(nqn);
    dout(4) << "Set failower GW " << gw_id << " for ANA group " << (int)ANA_groupid << dendl;
    return 0;
}


int NVMeofGwMap::process_gw_map_gw_down(uint16_t gw_id, const std::string& nqn)
{
#define ILLEGAL_GW_ID 0xFFF
#define MIN_NUM_ANA_GROUPS 0xFFF
    int rc = 0;
   // bool found = 0;
    int i;
    int min_num_ana_groups_in_gw = 0;
    int current_ana_groups_in_gw = 0;
    uint16_t min_loaded_gw_id = 0;
    GW_STATE_T* gw_state = find_gw_map(gw_id, nqn);
    if (gw_state) {
        dout(4) << "GW down " << gw_id << dendl;
        auto subsyst_it = find_subsystem_map(nqn);
        gw_state->availability = GW_AVAILABILITY_E::GW_UNAVAILABLE;
        for (i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++) {  // this GW may handle several ANA groups and  for each of them need to found the candidate GW
            if (gw_state->sm_state[i] == GW_ACTIVE_STATE) {
                // Find a GW that takes over the ANA group(s)

                min_num_ana_groups_in_gw = MIN_NUM_ANA_GROUPS;
                min_loaded_gw_id = ILLEGAL_GW_ID;
                for (auto& itr : *subsyst_it) { // for all the gateways of the subsystem
                    if (itr.second.availability == GW_AVAILABILITY_E::GW_AVAILABLE) {

                        current_ana_groups_in_gw = 0;
                        for (int j = 0; j < MAX_SUPPORTED_ANA_GROUPS; j++) {
                            if (itr.second.sm_state[j] == GW_BLOCKED_AGROUP_OWNER) {
                                current_ana_groups_in_gw = 0xFFFF;
                                break; // dont take into account these GWs in the transitive state
                            }
                            else if (itr.second.sm_state[j] == GW_ACTIVE_STATE)
                                current_ana_groups_in_gw++; // how many ANA groups are handled by this GW
                        }

                        if (min_num_ana_groups_in_gw > current_ana_groups_in_gw) {
                            min_num_ana_groups_in_gw = current_ana_groups_in_gw;
                            min_loaded_gw_id = itr.second.gw_id;
                            dout(4) << "choose: gw-id  min_ana_groups " << itr.second.gw_id << current_ana_groups_in_gw << " min " << min_num_ana_groups_in_gw << dendl;
                        }
                    }
                }
                if (min_loaded_gw_id != ILLEGAL_GW_ID) {
                    set_failover_gw_for_ANA_group(min_loaded_gw_id, nqn, i);
                }
                gw_state->sm_state[i] = GW_STANDBY_STATE;
            }
        }
    }
    else {
        dout(4)  << __FUNCTION__ << "ERROR GW-id was not found in the map " << gw_id << dendl;
        rc = 1;
    }
    return rc;
}


