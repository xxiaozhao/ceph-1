// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2024
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <iostream>
#include "common/ceph_argparse.h"
#include "common/debug.h"
#include "include/ceph_assert.h"
#include "global/global_init.h"
#include "mon/NVMeofGwMon.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_mon
#undef dout_prefix
#define dout_prefix *_dout

using namespace std;

void test_NVMeofGwMap() {
  dout(0) << __func__ << "\n\n" << dendl;

  NVMeofGwMap pending_map;
  std::string pool = "pool1";
  std::string group = "grp1";
  auto group_key = std::make_pair(pool, group);
  pending_map.cfg_add_gw("GW1" ,group_key);
  pending_map.cfg_add_gw("GW2" ,group_key);
  pending_map.cfg_add_gw("GW3" ,group_key);
  NONCE_VECTOR_T new_nonces = {"abc", "def","hij"};
  pending_map.Created_gws[group_key]["GW1"].nonce_map[1] = new_nonces;
  for(int i=0; i< MAX_SUPPORTED_ANA_GROUPS; i++)
    pending_map.Created_gws[group_key]["GW1"].blocklist_data[i].osd_epoch = i*2;

  pending_map.Created_gws[group_key]["GW2"].nonce_map[2] = new_nonces;
  GW_STATE_T gst1(1);
  pending_map.Gmap[group_key]["GW2"] = gst1;

  GW_STATE_T gst2(2);
  pending_map.Gmap[group_key]["GW3"] = gst2;
  dout(0) << pending_map << dendl;

  ceph::buffer::list bl;
  pending_map.encode(bl);
  auto p = bl.cbegin();
  pending_map.decode(p);
  dout(0) << "Dump map after decode encode:" <<dendl;
  dout(0) << pending_map << dendl;
}

void test_NVMeofGwMap_handle_removed() {
  dout(0) << __func__ << "\n\n" << dendl;
  auto group_key = std::make_pair("pool", "group");
  std::string nqn = "nqn2008.node1";

  NVMeofGwMap pending_map;
  pending_map.Gmap[group_key]["GW1"] = GW_STATE_T(1);
  pending_map.Gmap[group_key]["GW2"] = GW_STATE_T(2);
  pending_map.Gmap[group_key]["GW3"] = GW_STATE_T(2);
  dout(0) << "Initial: " << pending_map << dendl;

  /*bool proposed;
  pending_map.handle_removed_subsystems("GW2", group_key, {}, proposed);
  dout(0) << "After remove: " << pending_map << dendl;
  ceph_assert(proposed);
  auto& nqn_map = pending_map.Gmap[group_key];
  ceph_assert(nqn_map.size() == 2);
  ceph_assert(nqn_map.find("GW2") == nqn_map.end());

  pending_map.handle_removed_subsystems("GW1", group_key, { nqn }, proposed);
  dout(0) << "After non proposed remove: " << pending_map << dendl;
  ceph_assert(!proposed);
  ceph_assert(nqn_map.size() == 2);*/
}


int main(int argc, const char **argv)
{
  // Init ceph
  auto args = argv_to_vec(argc, argv);
  auto cct = global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT,
                         CODE_ENVIRONMENT_UTILITY,
                         CINIT_FLAG_NO_DEFAULT_CONFIG_FILE);
  common_init_finish(g_ceph_context);

  // Run tests
  test_NVMeofGwMap();
  test_NVMeofGwMap_handle_removed();
}

