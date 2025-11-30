#include "os/pfhandler.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

extern void * g_region;
extern size_t g_region_pages;
extern size_t g_page_sz;

int main()
{
    install_handler(nullptr, 4);
    std::cout << "managed region: " << g_region << " (" << g_region_pages
              << " pages, PROT_NONE)\n";

    volatile char* p = static_cast<char*>(g_region);
    char c = p[0] ;                  // 触发 SIGSEGV → handler 恢复权限并填充页面
    std::cout << "value=" << p[0] << '\n';

    munmap(g_region, g_region_pages * g_page_sz);
    return 0;
}