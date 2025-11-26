#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif
//！！！！！重要！！！！ 本文件中的函数完全来自caspp page 630-631

/* Constants ------------------------------------------------------------- */
#define RIO_BUFSIZE 8192  /* rio缓冲区大小 */
/* Enumerations ---------------------------------------------------------- */

/* Structures ------------------------------------------------------------ */
typedef struct {
    int rio_fd;                /* 描述符 */
    int rio_cnt;               /* 未读字节数 */
    char *rio_bufptr;          /* 下一个未读字节的指针 */
    char rio_buf[RIO_BUFSIZE]; /* 内部缓冲区 */
} rio_t;
/* Helpers --------------------------------------------------------------- */

/* API ------------------------------------------------------------------- */
void rio_readinit(rio_t *rp, int fd);
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readn(rio_t *rp, void *usrbuf, size_t n);

#if defined(__cplusplus)
}
#endif

#endif /* NET_PROTOCOL_H */
