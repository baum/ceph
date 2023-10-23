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


class MNVMeofGwBeacon final : public PaxosServiceMessage {
private:
  static constexpr int HEAD_VERSION = 1;
  static constexpr int COMPAT_VERSION = 1
  ;

protected:
  bool available;
  std::string name;
  NVMeofGwMap map;

public:
  MNVMeofGwBeacon()
    : PaxosServiceMessage{MSG_MNVMEOF_GW_BEACON, 0, HEAD_VERSION, COMPAT_VERSION},
      available(false)
  {}

  MNVMeofGwBeacon(const std::string &name_, const NVMeofGwMap &map_)
    : PaxosServiceMessage{MSG_MNVMEOF_GW_BEACON, 0, HEAD_VERSION, COMPAT_VERSION},
      available(false), name(name_), map(map_)
  {}

  bool get_available() const { return available; }
  const std::string& get_name() const { return name; }
  const NVMeofGwMap& get_map() const { return map; }

private:
  ~MNVMeofGwBeacon() final {}

public:

  std::string_view get_type_name() const override { return "nvmeofgwbeacon"; }

  void print(std::ostream& out) const override {
    out << get_type_name() << " nvmeofgw." << name << "("
    // ../src/messages/MNVMeofGwBeacon.h:59:29: error: no match for ‘operator<<’ (operand types are ‘std::basic_ostream<char>’ and ‘const NVMeofGwMap’)
	    << name << ", " << "map instance should be here" << ", " << available
	    << ")";
  }

  void encode_payload(uint64_t features) override {
    header.version = HEAD_VERSION;
    header.compat_version = COMPAT_VERSION;
    using ceph::encode;
    paxos_encode();

    encode(available, payload);
    encode(name, payload);
    // ../src/messages/MNVMeofGwBeacon.h:72:11: error: no matching function for call to ‘encode(NVMeofGwMap&, ceph::buffer::v15_2_0::list&)’
    //encode(map, payload);
  }
  void decode_payload() override {
    using ceph::decode;
    auto p = payload.cbegin();
    paxos_decode(p);
    decode(available, p);
    decode(name, p);
    // ../src/messages/MNVMeofGwBeacon.h:82:11: error: no matching function for call to ‘decode(NVMeofGwMap&, ceph::buffer::v15_2_0::list::iterator_impl<true>&)’
    //decode(map, p);
  }
private:
  template<class T, typename... Args>
  friend boost::intrusive_ptr<T> ceph::make_message(Args&&... args);
};


#endif
