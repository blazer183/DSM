#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h> 
#include <sys/types.h>
#include <sys/socket.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
#include "os/page_table.h"

extern void *SharedAddrBase;
extern class PageTable* PageTable;

void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body) {
    
    uint32_t res_id = body.resource_id;
    int new_owner = body.new_owner_id;

    ::PageTable->GlobalMutexLock();

    auto* record = ::PageTable->Find(res_id);

    ::PageTable->LocalMutexLock(res_id);
    ::PageTable->GlobalMutexUnlock();

    record->owner_id = new_owner;

    ::PageTable->LocalMutexUnlock(res_id);

    return;
    
}

