//包含pull_remote_page(page_base); getnodeid(); get_shared_memsize(); get_shared_addrbase();

#include "net/protocol.h"
#include "net/dsm_protocol.h"
#include "dsm.h"

void pull_remote_page(uintptr_t page_base){
    
}

int dsm_getnodeid(void){
    return NodeId;
}

size_t get_shared_memsize(void){
    //网络通信实现
}

void* get_shared_addrbase(void){
    //网络通信实现
}