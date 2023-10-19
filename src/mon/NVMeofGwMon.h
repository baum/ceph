/*
 * NVMeGWMonitor.h
 *
 *  Created on: Oct 17, 2023
 *      Author: 227870756
 */

#ifndef  MON_NVMEGWMONITOR_H_
#define  MON_NVMEGWMONITOR_H_
#include <map>
#include <set>

#include "include/Context.h"
//#include "MgrMap.h"
#include "PaxosService.h"
#include "MonCommand.h"
#include "NVMeofGwMap.h"

class NVMeofGwMon: public PaxosService
{
	NVMeofGwMap map;  //NVMeGWMap
  //MgrMap pending_map;
  //utime_t first_seen_inactive;

  std::map<uint64_t, ceph::coarse_mono_clock::time_point> last_beacon;


  // when the mon was not updating us for some period (e.g. during slow
  // election) to reset last_beacon timeouts
  ceph::coarse_mono_clock::time_point last_tick;

  std::vector<MonCommand> command_descs;
  std::vector<MonCommand> pending_command_descs;

public:
  NVMeofGwMon(Monitor &mn, Paxos &p, const std::string& service_name)
    : PaxosService(mn, p, service_name)
  {}
  ~NVMeofGwMon() override {}


  //const MgrMap &get_map() const { return map; }

 // bool in_use() const { return map.epoch > 0; }

  //void prime_mgr_client();


 // void get_store_prefixes(std::set<std::string>& s) const override;

  // 3 pure virtual methods of the paxosService
  void create_initial()override{};
  void create_pending()override{};
  void encode_pending(MonitorDBStore::TransactionRef t)override{};


  void init() override;
  void on_shutdown() override;
  void update_from_paxos(bool *need_bootstrap) override;


  bool preprocess_query(MonOpRequestRef op) override;
  bool prepare_update(MonOpRequestRef op) override;

  bool preprocess_command(MonOpRequestRef op);
  bool prepare_command(MonOpRequestRef op);

  void encode_full(MonitorDBStore::TransactionRef t) override { }

  bool preprocess_beacon(MonOpRequestRef op);
  bool prepare_beacon(MonOpRequestRef op);

  //void check_sub(Subscription *sub);
  //void check_subs()

  void tick() override;

  void print_summary(ceph::Formatter *f, std::ostream *ss) const;

  //const std::vector<MonCommand> &get_command_descs() const;


  //void get_versions(std::map<std::string, std::list<std::string>> &versions);


};



#endif /* SRC_MON_NVMEGWMONITOR_H_ */
