/*
 * NVMeofGwTypes.h
 *
 *  Created on: Dec 29, 2023
 */

#ifndef MON_NVMEOFGWTYPES_H_
#define MON_NVMEOFGWTYPES_H_
#include <string>
#include <iomanip>
#include <map>
#include <iostream>

using GW_ID_T      = std::string;
using GROUP_KEY    = std::pair<std::string, std::string>;
using NQN_ID_T     = std::string;
using ANA_GRP_ID_T = uint32_t;


enum class GW_STATES_PER_AGROUP_E {
    GW_IDLE_STATE = 0, //invalid state
    GW_STANDBY_STATE,
    GW_ACTIVE_STATE,
    GW_BLOCKED_AGROUP_OWNER,
    GW_WAIT_FAILBACK_PREPARED,
    GW_WAIT_FAILOVER_PREPARED // wait blocklist completed
};

enum class GW_EXPORTED_STATES_PER_AGROUP_E {
    GW_EXPORTED_OPTIMIZED_STATE = 0,
    GW_EXPORTED_INACCESSIBLE_STATE
};

enum class GW_AVAILABILITY_E {
    GW_CREATED = 0,
    GW_AVAILABLE,
    GW_UNAVAILABLE,
    GW_DELETED
};

#define MAX_SUPPORTED_ANA_GROUPS 16
#define INVALID_GW_TIMER     0xffff
#define REDUNDANT_GW_ANA_GROUP_ID 0xFF

typedef GW_STATES_PER_AGROUP_E          SM_STATE         [MAX_SUPPORTED_ANA_GROUPS];

using ANA_STATE = std::vector<GW_EXPORTED_STATES_PER_AGROUP_E>;

struct BeaconNamespace {
    ANA_GRP_ID_T anagrpid;
    std::string  nonce;
};

struct BeaconListener {
    std::string address_family; // IPv4 or IPv6
    std::string address;        //
    std::string svcid;          // port
};

struct BeaconSubsystem {
    NQN_ID_T nqn;
    std::list<BeaconListener>  listeners;
    std::list<BeaconNamespace> namespaces;
};

using BeaconSubsystems = std::list<BeaconSubsystem>;

struct NqnState {
    std::string   nqn;          // subsystem NQN
    ANA_STATE     ana_state;    // subsystem's ANA state

    // constructors
    NqnState(const std::string& _nqn, const ANA_STATE& _ana_state):
        nqn(_nqn), ana_state(_ana_state)  {}
    NqnState(const std::string& _nqn, const SM_STATE& sm_state) : nqn(_nqn)  {
        for (int i=0; i < MAX_SUPPORTED_ANA_GROUPS; i++)
            ana_state.push_back(sm_state[i] == GW_STATES_PER_AGROUP_E::GW_ACTIVE_STATE
                           ? GW_EXPORTED_STATES_PER_AGROUP_E::GW_EXPORTED_OPTIMIZED_STATE
                           : GW_EXPORTED_STATES_PER_AGROUP_E::GW_EXPORTED_INACCESSIBLE_STATE);
    }
};

typedef std::map<NQN_ID_T, NqnState> GwSubsystems;

struct GW_STATE_T {
    ANA_GRP_ID_T              group_id;
    uint64_t                  version;                       // reserved for future usage TBD
    GwSubsystems              subsystems;

    GW_STATE_T(ANA_GRP_ID_T id):
        group_id(id),
        version(0)
    {};

    GW_STATE_T() : GW_STATE_T(REDUNDANT_GW_ANA_GROUP_ID) {};
};

using NONCE_VECTOR_T    = std::vector<std::string>;
using GW_ANA_NONCE_MAP  = std::map <ANA_GRP_ID_T, NONCE_VECTOR_T>;

struct GW_CREATED_T {
    ANA_GRP_ID_T       ana_grp_id;                    // ana-group-id allocated for this GW, GW owns this group-id
    GW_AVAILABILITY_E  availability;                  // in absence of  beacon  heartbeat messages it becomes inavailable
    BeaconSubsystems   subsystems;                    // gateway susbsystem and their state machine states
    GW_ANA_NONCE_MAP   nonce_map;
    SM_STATE           sm_state;                      // state machine states per ANA group
    GW_ID_T            failover_peer[MAX_SUPPORTED_ANA_GROUPS];
    struct{
       epoch_t     osd_epoch;
    }blocklist_data[MAX_SUPPORTED_ANA_GROUPS];

    GW_CREATED_T(): ana_grp_id(REDUNDANT_GW_ANA_GROUP_ID) {};

    GW_CREATED_T(ANA_GRP_ID_T id): ana_grp_id(id), availability(GW_AVAILABILITY_E::GW_CREATED)
    {
        for (int i = 0; i < MAX_SUPPORTED_ANA_GROUPS; i++){
            sm_state[i] = GW_STATES_PER_AGROUP_E::GW_STANDBY_STATE;
            failover_peer[i]  = "";
            blocklist_data[i].osd_epoch = 0xffffffff;
        }
    };

    void standby_state(ANA_GRP_ID_T grpid) {
           sm_state[grpid]       = GW_STATES_PER_AGROUP_E::GW_STANDBY_STATE;
           failover_peer[grpid]  = "";
    };
};



struct GW_METADATA_T {
   struct{
      uint32_t     anagrp_sm_tstamps; // statemachine timer(timestamp) set in some state
      uint8_t      timer_value;
   } data[MAX_SUPPORTED_ANA_GROUPS];

    GW_METADATA_T() {
        for (int i=0; i<MAX_SUPPORTED_ANA_GROUPS; i++){
            data[i].anagrp_sm_tstamps = INVALID_GW_TIMER;
            data[i].timer_value = 0;
        }
    };
};

using GWMAP               = std::map<GW_ID_T, GW_STATE_T>;
using GWMETADATA          = std::map<GW_ID_T, GW_METADATA_T>;






using GW_CREATED_MAP      = std::map<GW_ID_T, GW_CREATED_T>;

#endif /* SRC_MON_NVMEOFGWTYPES_H_ */
