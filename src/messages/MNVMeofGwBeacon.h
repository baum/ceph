// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2023
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_NVMEOFGWBEACON_H
#define CEPH_NVMEOFGWBEACON_H

#include "messages/PaxosServiceMessage.h"
#include "mon/MonCommand.h"
#include "mon/NVMeofGwMap.h"

#include "include/types.h"

typedef GW_STATES_PER_AGROUP_E SM_STATE[MAX_SUPPORTED_ANA_GROUPS];

std::ostream& operator<<(std::ostream& os, const SM_STATE& value) {
    os << "SM_STATE [ "
    for ( auto e: value )
    switch (e) {
        case GW_STATES_PER_AGROUP_E::GW_IDLE_STATE: os << "IDLE "; break;
        case GW_STATES_PER_AGROUP_E::GW_STANDBY_STATE: os << "STANDBY "; break;
        case GW_STATES_PER_AGROUP_E::GW_ACTIVE_STATE: os << "ACTIVE "; break;
        case GW_STATES_PER_AGROUP_E::GW_BLOCKED_AGROUP_OWNER: os << "BLOCKED_AGROUP_OWNER "; break;
        case GW_STATES_PER_AGROUP_E::GW_WAIT_FAILBACK_PREPARED: os << "WAIT_FAILBACK_PREPARED "; break;
        default: os << "Invalid";
    }
    os << "]"
    return os;
}

std::ostream& operator<<(std::ostream& os, const GW_AVAILABILITY_E& value) {
  switch (e) {

        case GW_AVAILABILITY_E::GW_CREATED: os << "CREATED"; break;
        case GW_AVAILABILITY_E::GW_AVAILABLE: os << "AVAILABLE"; break;
        case GW_AVAILABILITY_E::GW_UNAVAILABLE: os << "UNAVAILABLE"; break;

        default: os << "Invalid";
    }
    return os;
}

class MNVMeofGwBeacon final : public PaxosServiceMessage {
private:
  static constexpr int HEAD_VERSION = 1;
  static constexpr int COMPAT_VERSION = 1;

protected:
    //bool                    ana_state[MAX_SUPPORTED_ANA_GROUPS]; // real ana states per ANA group for this GW :1- optimized, 0- inaccessible
    std::string              gw_id;
    SM_STATE                 sm_state;                             // state machine states per ANA group
    uint16_t                 opt_ana_gid;                          // optimized ANA group index as configured by Conf upon network entry, note for redundant GW it is FF
    GW_AVAILABILITY_E        availability;                         // in absence of  beacon  heartbeat messages it becomes inavailable

    uint64_t                 version;

public:
  MNVMeofGwBeacon()
    : PaxosServiceMessage{MSG_MNVMEOF_GW_BEACON, 0, HEAD_VERSION, COMPAT_VERSION}
  {}

  MNVMeofGwBeacon(const std::string &gw_id_, 
        const GW_STATES_PER_AGROUP_E (&sm_state_)[MAX_SUPPORTED_ANA_GROUPS],
        const uint16_t& opt_ana_gid_,
        const GW_AVAILABILITY_E  availability_,
        const uint64_t& version_
  )
    : PaxosServiceMessage{MSG_MNVMEOF_GW_BEACON, 0, HEAD_VERSION, COMPAT_VERSION},
      gw_id(gw_id_), sm_state(state_), opt_ana_gid(opt_ana_gid_),
      availability(availability_), version(version_)
  {}

  const std::string& get_gw_id() const { return gw_id; }
  const uint16_t& get_opt_ana_gid() const { return opt_ana_gid; }
  const GW_AVAILABILITY_E& get_availability() const { return availability; }
  const uint64_t& get_version() const { return version; }
  const SM_STATE& get_sm_state() const { return sm_state; };

private:
  ~MNVMeofGwBeacon() final {}

public:

  std::string_view get_type_name() const override { return "nvmeofgwbeacon"; }

  void print(std::ostream& out) const override {
    out << get_type_name() << " nvmeofgw." << name << "("
    // ../src/messages/MNVMeofGwBeacon.h:59:29: error: no match for ‘operator<<’ (operand types are ‘std::basic_ostream<char>’ and ‘const NVMeofGwMap’)
	    << gw_id <<  ", " << sm_state << "," << opt_ana_gid << "," << availability << "," << version
	    << ")";
  }

  void encode_payload(uint64_t features) override {
    header.version = HEAD_VERSION;
    header.compat_version = COMPAT_VERSION;
    using ceph::encode;
    paxos_encode();
    encode(name, payload);
    encode(state, p)
    // ../src/messages/MNVMeofGwBeacon.h:72:11: error: no matching function for call to ‘encode(NVMeofGwMap&, ceph::buffer::v15_2_0::list&)’
  }
  void decode_payload() override {
    using ceph::decode;
    auto p = payload.cbegin();
    paxos_decode(p);
    decode(name, p);
    // ../src/messages/MNVMeofGwBeacon.h:82:11: error: no matching function for call to ‘decode(NVMeofGwMap&, ceph::buffer::v15_2_0::list::iterator_impl<true>&)’
    decode(state, p);
  }
private:
  template<class T, typename... Args>
  friend boost::intrusive_ptr<T> ceph::make_message(Args&&... args);
};


#endif
