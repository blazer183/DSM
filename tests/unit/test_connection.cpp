#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

#include "net/protocol.h"

namespace {

bool write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written <= 0)
			return false;
		len -= static_cast<size_t>(written);
		buf += written;
	}
	return true;
}

void test_readn_exact()
{
	int fds[2];
	assert(pipe(fds) == 0);

	const char payload[] = "abcdef";
	assert(write_all(fds[1], payload, sizeof(payload) - 1));
	close(fds[1]);

	rio_t rio{};
	rio_readinit(&rio, fds[0]);

	char buf[4] = {0};
	ssize_t n = rio_readn(&rio, buf, 3);
	assert(n == 3);
	assert(std::strncmp(buf, "abc", 3) == 0);

	char remaining[8] = {0};
	ssize_t partial = rio_readn(&rio, remaining, sizeof(remaining));
	assert(partial == 3);
	assert(std::strncmp(remaining, "def", 3) == 0);

	assert(rio_readn(&rio, remaining, 1) == 0); /* EOF */
	close(fds[0]);
}

void test_readline()
{
	int fds[2];
	assert(pipe(fds) == 0);

	const std::string text = "first line\nsecond line\n";
	assert(write_all(fds[1], text.c_str(), text.size()));
	close(fds[1]);

	rio_t rio{};
	rio_readinit(&rio, fds[0]);

	char line[64];
	ssize_t len = rio_readline(&rio, line, sizeof(line));
	assert(len == 10 + 1); /* includes newline */
	assert(std::string(line) == "first line\n");

	len = rio_readline(&rio, line, sizeof(line));
	assert(len == 11 + 1);
	assert(std::string(line) == "second line\n");

	/* EOF after consuming both lines */
	assert(rio_readline(&rio, line, sizeof(line)) == 0);
	close(fds[0]);
}

} // namespace

int main()
{
	test_readn_exact();
	test_readline();

	std::cout << "All RIO tests passed" << std::endl;
	return 0;
}
