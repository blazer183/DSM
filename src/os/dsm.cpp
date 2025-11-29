//包含dsm_init , 会调用client_end里的getnodeid(); get_shared_memsize(); get_shared_addrbase();
#include "dsm.h"


int dsm_init(int argc, char *argv[], int dsm_memsize){
    //第一部分，创建监听线程，监听线程实现代码与声明
}
