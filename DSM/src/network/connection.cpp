#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "net/protocol.h"

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
	int cnt;

	while (rp->rio_cnt <= 0) {
		rp->rio_cnt = (int)read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
		if (rp->rio_cnt < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		} else if (rp->rio_cnt == 0) {
			return 0;
		} else {
			rp->rio_bufptr = rp->rio_buf;
		}
	}

	cnt = (int)n;
	if (rp->rio_cnt < cnt)
		cnt = rp->rio_cnt;
	memcpy(usrbuf, rp->rio_bufptr, (size_t)cnt);
	rp->rio_bufptr += cnt;
	rp->rio_cnt -= cnt;
	return cnt;
}

void rio_readinit(rio_t *rp, int fd)
{
	if (rp == NULL)
		return;
	rp->rio_fd = fd;
	rp->rio_cnt = 0;
	rp->rio_bufptr = rp->rio_buf;
}

//读整行/整个缓冲区（一行过长）
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen)
{
	int n, rc;
	char c, *bufp = (char*) usrbuf;

	for (n = 1; (size_t)n < maxlen; n++) {
		if ((rc = (int)rio_read(rp, &c, 1)) == 1) {
			*bufp++ = c;
			if (c == '\n') {
				n++;
				break;
			}
		} else if (rc == 0) {
			if (n == 1)
				return 0;
			break;
		} else {
			return -1;
		}
	}

	*bufp = 0;
	return n - 1;
}

//读固定字节大小
ssize_t rio_readn(rio_t *rp, void *usrbuf, size_t n)
{
	size_t nleft = n;
	ssize_t nread;
	char *bufp = (char*) usrbuf;

	while (nleft > 0) {
		if ((nread = rio_read(rp, bufp, nleft)) < 0)
			return -1;
		else if (nread == 0)
			break;
		nleft -= (size_t)nread;
		bufp += nread;
	}
	return (ssize_t)(n - nleft);
}


