#ifndef PFHANDLER_H
#define PFHANDLER_H 

#include <cstddef>
#include <cstdint>

extern size_t SharedPages;                  //
extern int *InvalidPages ;     

void install_handler(void* base_addr, size_t num_pages);

void pull_remote_page(int VPN);

#endif
