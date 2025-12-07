#ifndef PFHANDLER_H
#define PFHANDLER_H 

#include <cstddef>
#include <cstdint>

extern size_t SharedPages;                  //
extern int *InvalidPages ;     

void install_handler(void* base_addr, size_t num_pages);

void pull_remote_page(uintptr_t page_base);

#endif



