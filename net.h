#ifndef XP_NET_H_
#define XP_NET_H_

#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include "logger.h"
#include <cassert>
#include <stdexcept>
#include <exception>

namespace xp
{
	static int set_fd_nonblock(int fd) noexcept
	{
		const int flags = fcntl(fd, F_GETFL, 0);
		if ((flags == -1) || (flags & O_NONBLOCK))
		{
			return flags;
		}
		else
		{
			return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		}
	}

	struct Acceptor
	{
		int port;
		int fd;
		sockaddr_in addr;

		Acceptor(const int p = 2100)
			: port(p),
			  fd(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
		{
			log(fmt::format("fd={}", fd));
			if (fd < 0)
			{
				log(fmt::format("fd={}", fd), "error");
				throw std::runtime_error("Acceptor fd < 0\n");
			}
			set_fd_nonblock(fd);
			bind();
		}
		Acceptor(const Acceptor &) = delete;
		Acceptor &operator=(const Acceptor &) = delete;
		Acceptor(Acceptor &&) = delete;
		Acceptor &operator=(Acceptor &&) = delete;
		~Acceptor()
		{
			::close(fd);
		}
		int bind() noexcept
		{
			::bzero(&addr, sizeof(struct sockaddr_in));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
			const int res = ::bind(fd, (struct sockaddr *)&addr, sizeof(addr));
			assert(res >= 0);
			return res;
		}
		int listen(int backlog = 21)
		{
			return ::listen(fd, backlog);
		}
		/*std::tuple<int, sockaddr_in> accept() noexcept
		{
			sockaddr_in addr;
			socklen_t len = sizeof(addr);
			const int new_fd = ::accept(fd, (sockaddr *)&addr, &len);
			return {new_fd, addr};
		}*/
		
	};

	
	
}

#endif // !XP_NET_H_
