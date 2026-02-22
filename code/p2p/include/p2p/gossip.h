#include "peers.h"

#ifndef GOSSIP_DT
#define GOSSIP_DT 3
#endif

#ifndef P2P_GOSSIP

#pragma pack(push, 1)
typedef struct {
    uint32_t uid;
    uint32_t ip;
    uint16_t port;
} gossip_entry;
#pragma pack(pop)

typedef struct {
    prot_array gossips;
    uint32_t   selfuid;
} gossip_system;

int gossip_system_init(gossip_system *sys, uint32_t selfuid){
    sys->gossips = prot_array_create(sizeof(gossip_entry));
    sys->selfuid = selfuid;
    return 0;
}

void gossip_system_end(gossip_system *sys){
    prot_array_end(&sys->gossips);
}

int gossip_new_entry(gossip_system *sys, gossip_entry entry){
    if (prot_array_count(&sys->gossips, &entry) != 0 || entry.uid == sys->selfuid) {
        return -1;
    }
    return prot_array_push(&sys->gossips, &entry);
}

int gossip_system_clear(gossip_system *sys){
    prot_array_lock(&sys->gossips);
    for (size_t i = 0; i < sys->gossips.array.len; i++){
        if (0 > prot_array_remove(&sys->gossips, 0)) {
            prot_array_unlock(&sys->gossips);
            return -1;
        }
    }
    prot_array_unlock(&sys->gossips);
    return 0;
}

int gossip_system_update(
    gossip_system *sys,
    void *gossip_data, size_t gossip_dsize
){
    if (!sys) return -1;
    
    if (gossip_dsize % sizeof(gossip_entry) != 0) {
        SLOG_ERROR("[gossip] failed to update local DB, incoming data has invalid size (%zu)", gossip_dsize);
        return -1;
    }

    dyn_array local_array;
    local_array.head = 1;
    local_array.element_size = sizeof(gossip_entry);
    local_array.len = gossip_dsize / sizeof(gossip_entry);
    local_array.elements = gossip_data;

    
    prot_array_lock(&sys->gossips);
    size_t old_l = sys->gossips.array.len;
    for (size_t i = 0; i < local_array.len; i++){
        gossip_new_entry(sys, 
            *((gossip_entry*)dyn_array_at(&local_array, i))
        );
    }
    if (old_l != sys->gossips.array.len){
        SLOG_INFO("[gossip] acquired %zu new gossip entries", sys->gossips.array.len - old_l);
    }
    prot_array_unlock(&sys->gossips);
    return 0;
}

udp_packet *gossip_make_packet(
    gossip_system *system,
    uint32_t      from,
    uint32_t      to
){
    prot_array_lock(&system->gossips);
    udp_packet *pack = udp_make_pack(
        0, from, 0, 
        P2P_PACK_GOSSIP, system->gossips.array.elements, 
        sizeof(gossip_entry) * system->gossips.array.len
    );
    prot_array_unlock(&system->gossips);
    return pack;
}

#endif
#define P2P_GOSSIP

