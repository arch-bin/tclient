/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "net.h"

#include "dbg.h"
#include "log.h"
#include "mem.h"
#include "str.h"
#include "windows.h"

#include <chrono>
#include <iterator> // std::size
#include <string_view>

#if defined(CONF_OPENSSL)
// TClient: Shadowsocks AEAD UDP relay uses OpenSSL for key derivation and ciphers.
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#endif

#if defined(CONF_WEBSOCKETS)
#include <engine/shared/websockets.h>
#endif

#if defined(CONF_FAMILY_UNIX)
#include <sys/time.h> // timeval
#include <unistd.h> // close

// UNIX net includes
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#if defined(CONF_PLATFORM_SOLARIS)
#include <sys/filio.h> // FIONBIO
#endif

#include <cerrno>
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error NOT IMPLEMENTED
#endif

static NETSTATS network_stats = {0};

#define VLEN 128
#define PACKETSIZE 1400
typedef struct
{
#ifdef CONF_PLATFORM_LINUX
	int pos;
	int size;
	struct mmsghdr msgs[VLEN];
	struct iovec iovecs[VLEN];
	char bufs[VLEN][PACKETSIZE];
	char sockaddrs[VLEN][128];
#else
	char buf[PACKETSIZE];
#endif
} NETSOCKET_BUFFER;

void net_buffer_init(NETSOCKET_BUFFER *buffer)
{
#if defined(CONF_PLATFORM_LINUX)
	buffer->pos = 0;
	buffer->size = 0;
	mem_zero(buffer->msgs, sizeof(buffer->msgs));
	mem_zero(buffer->iovecs, sizeof(buffer->iovecs));
	mem_zero(buffer->sockaddrs, sizeof(buffer->sockaddrs));
	for(int i = 0; i < VLEN; ++i)
	{
		buffer->iovecs[i].iov_base = buffer->bufs[i];
		buffer->iovecs[i].iov_len = PACKETSIZE;
		buffer->msgs[i].msg_hdr.msg_iov = &(buffer->iovecs[i]);
		buffer->msgs[i].msg_hdr.msg_iovlen = 1;
		buffer->msgs[i].msg_hdr.msg_name = &(buffer->sockaddrs[i]);
		buffer->msgs[i].msg_hdr.msg_namelen = sizeof(buffer->sockaddrs[i]);
	}
#endif
}

void net_buffer_reinit(NETSOCKET_BUFFER *buffer)
{
#if defined(CONF_PLATFORM_LINUX)
	for(int i = 0; i < VLEN; i++)
	{
		buffer->msgs[i].msg_hdr.msg_namelen = sizeof(buffer->sockaddrs[i]);
	}
#endif
}

void net_buffer_simple(NETSOCKET_BUFFER *buffer, char **buf, int *size)
{
#if defined(CONF_PLATFORM_LINUX)
	*buf = buffer->bufs[0];
	*size = sizeof(buffer->bufs[0]);
#else
	*buf = buffer->buf;
	*size = sizeof(buffer->buf);
#endif
}

struct NETSOCKET_INTERNAL
{
	int type;
	int ipv4sock;
	int ipv6sock;
	int web_ipv4sock;
	int web_ipv6sock;

	// Proxy (TClient): when proxy_active is set, UDP traffic on this socket is relayed
	// through proxy_relay. For SOCKS5 (UDP ASSOCIATE) proxy_tcp holds the control
	// connection that keeps the association alive; for Shadowsocks the datagrams are
	// AEAD-encrypted with ss_key/ss_cipher and proxy_relay is the Shadowsocks server.
	int proxy_active;
	int proxy_type;
	int proxy_tcp;
	NETADDR proxy_relay;
	int ss_cipher;
	int ss_keylen;
	unsigned char ss_key[32];
	unsigned char proxy_scratch[PACKETSIZE]; // decrypted Shadowsocks payload handed back from recv

	NETSOCKET_BUFFER buffer;
};
static NETSOCKET_INTERNAL invalid_socket = {NETTYPE_INVALID, -1, -1, -1, -1, 0, 0, -1, {}, 0, 0, {}, {}, {}};

const NETADDR NETADDR_ZEROED = {NETTYPE_INVALID, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0};

static void netaddr_to_sockaddr_in(const NETADDR *src, sockaddr_in *dest)
{
	dbg_assert((src->type & NETTYPE_IPV4) != 0, "Invalid address type '%d' for netaddr_to_sockaddr_in", src->type);
	mem_zero(dest, sizeof(*dest));
	dest->sin_family = AF_INET;
	dest->sin_port = htons(src->port);
	mem_copy(&dest->sin_addr.s_addr, src->ip, 4);
}

static void netaddr_to_sockaddr_in6(const NETADDR *src, sockaddr_in6 *dest)
{
	dbg_assert((src->type & NETTYPE_IPV6) != 0, "Invalid address type '%d' for netaddr_to_sockaddr_in6", src->type);
	mem_zero(dest, sizeof(*dest));
	dest->sin6_family = AF_INET6;
	dest->sin6_port = htons(src->port);
	mem_copy(&dest->sin6_addr.s6_addr, src->ip, 16);
}

static void sockaddr_to_netaddr(const sockaddr *src, socklen_t src_len, NETADDR *dst)
{
	*dst = NETADDR_ZEROED;
	if(src->sa_family == AF_INET && src_len >= (socklen_t)sizeof(sockaddr_in))
	{
		const sockaddr_in *src_in = (const sockaddr_in *)src;
		dst->type = NETTYPE_IPV4;
		dst->port = htons(src_in->sin_port);
		static_assert(sizeof(dst->ip) >= sizeof(src_in->sin_addr.s_addr));
		mem_copy(dst->ip, &src_in->sin_addr.s_addr, sizeof(src_in->sin_addr.s_addr));
	}
	else if(src->sa_family == AF_INET6 && src_len >= (socklen_t)sizeof(sockaddr_in6))
	{
		const sockaddr_in6 *src_in6 = (const sockaddr_in6 *)src;
		dst->type = NETTYPE_IPV6;
		dst->port = htons(src_in6->sin6_port);
		static_assert(sizeof(dst->ip) >= sizeof(src_in6->sin6_addr.s6_addr));
		mem_copy(dst->ip, &src_in6->sin6_addr.s6_addr, sizeof(src_in6->sin6_addr.s6_addr));
	}
	else
	{
		log_warn("net", "Cannot convert sockaddr of family %d", src->sa_family);
	}
}

int net_addr_comp(const NETADDR *a, const NETADDR *b)
{
	int diff = a->type - b->type;
	if(diff != 0)
	{
		return diff;
	}
	diff = mem_comp(a->ip, b->ip, sizeof(a->ip));
	if(diff != 0)
	{
		return diff;
	}
	return a->port - b->port;
}

bool NETADDR::operator==(const NETADDR &other) const
{
	return net_addr_comp(this, &other) == 0;
}

bool NETADDR::operator!=(const NETADDR &other) const
{
	return net_addr_comp(this, &other) != 0;
}

bool NETADDR::operator<(const NETADDR &other) const
{
	return net_addr_comp(this, &other) < 0;
}

size_t std::hash<NETADDR>::operator()(const NETADDR &Addr) const noexcept
{
	size_t seed = std::hash<unsigned int>{}(Addr.type);
	seed ^= std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(Addr.ip), sizeof(Addr.ip))) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<unsigned short>{}(Addr.port) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	return seed;
}

int net_addr_comp_noport(const NETADDR *a, const NETADDR *b)
{
	int diff = a->type - b->type;
	if(diff != 0)
	{
		return diff;
	}
	return mem_comp(a->ip, b->ip, sizeof(a->ip));
}

void net_addr_str_v6(const unsigned short ip[8], int port, char *buffer, int buffer_size)
{
	int longest_seq_len = 0;
	int longest_seq_start = -1;
	int w = 0;
	int i;
	{
		int seq_len = 0;
		int seq_start = -1;
		// Determine longest sequence of zeros.
		for(i = 0; i < 8 + 1; i++)
		{
			if(seq_start != -1)
			{
				if(i == 8 || ip[i] != 0)
				{
					if(longest_seq_len < seq_len)
					{
						longest_seq_len = seq_len;
						longest_seq_start = seq_start;
					}
					seq_len = 0;
					seq_start = -1;
				}
				else
				{
					seq_len += 1;
				}
			}
			else
			{
				if(i != 8 && ip[i] == 0)
				{
					seq_start = i;
					seq_len = 1;
				}
			}
		}
	}
	if(longest_seq_len <= 1)
	{
		longest_seq_len = 0;
		longest_seq_start = -1;
	}
	w += str_copy(buffer + w, "[", buffer_size - w);
	for(i = 0; i < 8; i++)
	{
		if(longest_seq_start <= i && i < longest_seq_start + longest_seq_len)
		{
			if(i == longest_seq_start)
			{
				w += str_copy(buffer + w, "::", buffer_size - w);
			}
		}
		else
		{
			const char *colon = (i == 0 || i == longest_seq_start + longest_seq_len) ? "" : ":";
			w += str_format(buffer + w, buffer_size - w, "%s%x", colon, ip[i]);
		}
	}
	w += str_copy(buffer + w, "]", buffer_size - w);
	if(port >= 0)
	{
		str_format(buffer + w, buffer_size - w, ":%d", port);
	}
}

void net_addr_str(const NETADDR *addr, char *string, int max_length, bool add_port)
{
	if((addr->type & (NETTYPE_IPV4 | NETTYPE_WEBSOCKET_IPV4)) != 0)
	{
		if(add_port)
		{
			str_format(string, max_length, "%d.%d.%d.%d:%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3], addr->port);
		}
		else
		{
			str_format(string, max_length, "%d.%d.%d.%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
		}
	}
	else if((addr->type & (NETTYPE_IPV6 | NETTYPE_WEBSOCKET_IPV6)) != 0)
	{
		unsigned short ip[8];
		for(int i = 0; i < 8; i++)
		{
			ip[i] = (addr->ip[i * 2] << 8) | (addr->ip[i * 2 + 1]);
		}
		int port = add_port ? addr->port : -1;
		net_addr_str_v6(ip, port, string, max_length);
	}
	else
	{
		dbg_assert_failed("unknown NETADDR type %d", addr->type);
	}
}

static int parse_int(int *out, const char **str)
{
	int i = 0;
	*out = 0;
	if(!str_isnum(**str))
		return -1;

	i = **str - '0';
	(*str)++;

	while(true)
	{
		if(!str_isnum(**str))
		{
			*out = i;
			return 0;
		}

		i = (i * 10) + (**str - '0');
		(*str)++;
	}

	return 0;
}

static int parse_char(char c, const char **str)
{
	if(**str != c)
		return -1;
	(*str)++;
	return 0;
}

static int parse_uint8(unsigned char *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0)
		return -1;
	if(i < 0 || i > 0xff)
		return -1;
	*out = i;
	return 0;
}

static int parse_uint16(unsigned short *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0)
		return -1;
	if(i < 0 || i > 0xffff)
		return -1;
	*out = i;
	return 0;
}

int net_addr_from_url(NETADDR *addr, const char *string, char *host_buf, size_t host_buf_size)
{
	bool sixup = false;
	mem_zero(addr, sizeof(*addr));
	const char *str = str_startswith(string, "tw-0.6+udp://");
	if(!str && (str = str_startswith(string, "tw-0.7+udp://")))
	{
		addr->type |= NETTYPE_TW7;
		sixup = true;
	}
	if(!str)
		return 1;

	int length = str_length(str);
	int start = 0;
	int end = length;
	for(int i = 0; i < length; i++)
	{
		if(str[i] == '@')
		{
			if(start != 0)
			{
				// Two at signs.
				return true;
			}
			start = i + 1;
		}
		else if(str[i] == '/' || str[i] == '?' || str[i] == '#')
		{
			end = i;
			break;
		}
	}

	char host[128];
	str_truncate(host, sizeof(host), str + start, end - start);
	if(host_buf)
		str_copy(host_buf, host, host_buf_size);

	int failure = net_addr_from_str(addr, host);
	if(failure)
		return failure;

	if(sixup)
		addr->type |= NETTYPE_TW7;

	return failure;
}

bool net_addr_is_local(const NETADDR *addr)
{
	char addr_str[NETADDR_MAXSTRSIZE];
	net_addr_str(addr, addr_str, sizeof(addr_str), true);

	if(addr->ip[0] == 127 || addr->ip[0] == 10 || (addr->ip[0] == 192 && addr->ip[1] == 168) || (addr->ip[0] == 172 && (addr->ip[1] >= 16 && addr->ip[1] <= 31)))
		return true;

	if(str_startswith(addr_str, "[fe80:") || str_startswith(addr_str, "[::1"))
		return true;

	return false;
}

int net_addr_from_str(NETADDR *addr, const char *string)
{
	const char *str = string;
	mem_zero(addr, sizeof(NETADDR));

	if(str[0] == '[')
	{
		/* ipv6 */
		sockaddr_in6 sa6;
		char buf[128];
		int i;
		str++;
		for(i = 0; i < 127 && str[i] && str[i] != ']'; i++)
			buf[i] = str[i];
		buf[i] = 0;
		str += i;
#if defined(CONF_FAMILY_WINDOWS)
		{
			int size;
			sa6.sin6_family = AF_INET6;
			size = (int)sizeof(sa6);
			if(WSAStringToAddressA(buf, AF_INET6, nullptr, (sockaddr *)&sa6, &size) != 0)
				return -1;
		}
#else
		sa6.sin6_family = AF_INET6;

		if(inet_pton(AF_INET6, buf, &sa6.sin6_addr) != 1)
			return -1;
#endif
		sockaddr_to_netaddr((sockaddr *)&sa6, sizeof(sa6), addr);

		if(*str == ']')
		{
			str++;
			if(*str == ':')
			{
				str++;
				if(parse_uint16(&addr->port, &str))
					return -1;
			}
			else
			{
				addr->port = 0;
			}
		}
		else
			return -1;

		return 0;
	}
	else
	{
		/* ipv4 */
		if(parse_uint8(&addr->ip[0], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[1], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[2], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[3], &str))
			return -1;
		if(*str == ':')
		{
			str++;
			if(parse_uint16(&addr->port, &str))
				return -1;
		}
		if(*str != '\0')
			return -1;

		addr->type = NETTYPE_IPV4;
	}

	return 0;
}

static int priv_net_extract(const char *hostname, char *host, int max_host, int *port)
{
	*port = 0;
	host[0] = 0;

	if(hostname[0] == '[')
	{
		// ipv6 mode
		int i;
		for(i = 1; i < max_host && hostname[i] && hostname[i] != ']'; i++)
			host[i - 1] = hostname[i];
		host[i - 1] = 0;
		if(hostname[i] != ']') // malformatted
			return -1;

		i++;
		if(hostname[i] == ':')
			*port = str_toint(hostname + i + 1);
	}
	else
	{
		// generic mode (ipv4, hostname etc)
		int i;
		for(i = 0; i < max_host - 1 && hostname[i] && hostname[i] != ':'; i++)
			host[i] = hostname[i];
		host[i] = 0;

		if(hostname[i] == ':')
			*port = str_toint(hostname + i + 1);
	}

	return 0;
}

static int net_host_lookup_fallback(const char *hostname, NETADDR *addr, int types, int port)
{
	if(str_comp_nocase(hostname, "localhost") == 0)
	{
		if(types == NETTYPE_IPV4)
		{
			dbg_assert(net_addr_from_str(addr, "127.0.0.1") == 0, "unreachable");
			addr->port = port;
			return 0;
		}
		else if(types == NETTYPE_IPV6)
		{
			dbg_assert(net_addr_from_str(addr, "[::1]") == 0, "unreachable");
			addr->port = port;
			return 0;
		}
		else
		{
			// TODO: return both IPv4 and IPv6 address
			dbg_assert(net_addr_from_str(addr, "127.0.0.1") == 0, "unreachable");
			addr->port = port;
			return 0;
		}
	}
	return -1;
}

static int net_host_lookup_impl(const char *hostname, NETADDR *addr, int types)
{
	char host[256];
	int port = 0;
	if(priv_net_extract(hostname, host, sizeof(host), &port))
		return -1;

	log_trace("host_lookup", "host='%s' port='%d' types='%d'", host, port, types);

	struct addrinfo hints;
	mem_zero(&hints, sizeof(hints));

	if(types == NETTYPE_IPV4)
		hints.ai_family = AF_INET;
	else if(types == NETTYPE_IPV6)
		hints.ai_family = AF_INET6;
	else
		hints.ai_family = AF_UNSPEC;

	struct addrinfo *result = nullptr;
	int e = getaddrinfo(host, nullptr, &hints, &result);
	if(!result)
	{
		return net_host_lookup_fallback(hostname, addr, types, port);
	}

	if(e != 0)
	{
		freeaddrinfo(result);
		return net_host_lookup_fallback(hostname, addr, types, port);
	}

	sockaddr_to_netaddr(result->ai_addr, result->ai_addrlen, addr);
	addr->port = port;
	freeaddrinfo(result);
	return 0;
}

int net_host_lookup(const char *hostname, NETADDR *addr, int types)
{
	const char *ws_hostname = str_startswith(hostname, "ws://");
	if(ws_hostname)
	{
		if((types & (NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6)) == 0)
		{
			return -1;
		}
		int result = net_host_lookup_impl(ws_hostname, addr, types & ~(NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6));
		if(result == 0)
		{
			if(addr->type == NETTYPE_IPV4)
			{
				addr->type = NETTYPE_WEBSOCKET_IPV4;
			}
			else if(addr->type == NETTYPE_IPV6)
			{
				addr->type = NETTYPE_WEBSOCKET_IPV6;
			}
		}
		return result;
	}
	return net_host_lookup_impl(hostname, addr, types & ~(NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6));
}

void net_init()
{
#if defined(CONF_FAMILY_WINDOWS)
	WSADATA wsa_data;
	dbg_assert(WSAStartup(MAKEWORD(1, 1), &wsa_data) == 0, "WSAStartup failure");
#endif
#if defined(CONF_WEBSOCKETS)
	websocket_init();
#endif
}

int net_errno()
{
#if defined(CONF_FAMILY_WINDOWS)
	return WSAGetLastError();
#else
	return errno;
#endif
}

std::string net_error_message()
{
	const int error = net_errno();
#if defined(CONF_FAMILY_WINDOWS)
	const std::string message = windows_format_system_message(error);
	return std::to_string(error) + " '" + message + "'";
#else
	return std::to_string(error) + " '" + strerror(error) + "'";
#endif
}

void net_stats(NETSTATS *stats_inout)
{
	*stats_inout = network_stats;
}

int net_socket_type(NETSOCKET sock)
{
	return sock->type;
}

static int net_set_blocking_impl(NETSOCKET sock, bool blocking)
{
	unsigned long mode = blocking ? 0 : 1;
	const char *mode_str = blocking ? "blocking" : "non-blocking";
	int sockets[] = {sock->ipv4sock, sock->ipv6sock};
	const char *socket_str[] = {"IPv4", "IPv6"};

	for(size_t i = 0; i < std::size(sockets); ++i)
	{
		if(sockets[i] >= 0)
		{
#if defined(CONF_FAMILY_WINDOWS)
			if(ioctlsocket(sockets[i], FIONBIO, (unsigned long *)&mode) != NO_ERROR)
			{
				log_error("net", "Setting %s mode for %s socket failed (%s)", socket_str[i], mode_str, net_error_message().c_str());
			}
#else
			if(ioctl(sockets[i], FIONBIO, (unsigned long *)&mode) == -1)
			{
				log_error("net", "Setting %s mode for %s socket failed (%s)", socket_str[i], mode_str, net_error_message().c_str());
			}
#endif
		}
	}

	return 0;
}

int net_set_non_blocking(NETSOCKET sock)
{
	return net_set_blocking_impl(sock, false);
}

int net_set_blocking(NETSOCKET sock)
{
	return net_set_blocking_impl(sock, true);
}

int net_would_block()
{
#if defined(CONF_FAMILY_WINDOWS)
	return net_errno() == WSAEWOULDBLOCK;
#else
	return net_errno() == EWOULDBLOCK;
#endif
}

int net_socket_read_wait(NETSOCKET sock, std::chrono::nanoseconds nanoseconds)
{
	const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(nanoseconds).count();
	dbg_assert(microseconds >= 0, "Negative wait duration %" PRId64 " not allowed", microseconds);

	fd_set readfds;
	FD_ZERO(&readfds);

	int maxfd = -1;
	if(sock->ipv4sock >= 0)
	{
		FD_SET(sock->ipv4sock, &readfds);
		maxfd = sock->ipv4sock;
	}
	if(sock->ipv6sock >= 0)
	{
		FD_SET(sock->ipv6sock, &readfds);
		maxfd = std::max(maxfd, sock->ipv6sock);
	}
#if defined(CONF_WEBSOCKETS)
	if(sock->web_ipv4sock >= 0)
	{
		maxfd = std::max(maxfd, websocket_fd_set(sock->web_ipv4sock, &readfds));
	}
	if(sock->web_ipv6sock >= 0)
	{
		maxfd = std::max(maxfd, websocket_fd_set(sock->web_ipv6sock, &readfds));
	}
#endif
	if(maxfd < 0)
	{
		return 0;
	}

	struct timeval tv;
	tv.tv_sec = microseconds / 1000000;
	tv.tv_usec = microseconds % 1000000;
	// don't care about writefds and exceptfds
	select(maxfd + 1, &readfds, nullptr, nullptr, &tv);

	if(sock->ipv4sock >= 0 && FD_ISSET(sock->ipv4sock, &readfds))
	{
		return 1;
	}
	if(sock->ipv6sock >= 0 && FD_ISSET(sock->ipv6sock, &readfds))
	{
		return 1;
	}
#if defined(CONF_WEBSOCKETS)
	if(sock->web_ipv4sock >= 0 && websocket_fd_get(sock->web_ipv4sock, &readfds))
	{
		return 1;
	}
	if(sock->web_ipv6sock >= 0 && websocket_fd_get(sock->web_ipv6sock, &readfds))
	{
		return 1;
	}
#endif
	return 0;
}

static void priv_net_close_socket(int sock)
{
#if defined(CONF_FAMILY_WINDOWS)
	dbg_assert(closesocket(sock) == 0, "closesocket failure (%s)", net_error_message().c_str());
#else
	dbg_assert(close(sock) == 0, "close failure (%s)", net_error_message().c_str());
#endif
}

static void priv_net_close_all_sockets(NETSOCKET sock)
{
	if(sock->ipv4sock >= 0)
	{
		priv_net_close_socket(sock->ipv4sock);
		sock->ipv4sock = -1;
		sock->type &= ~NETTYPE_IPV4;
	}

#if defined(CONF_WEBSOCKETS)
	if(sock->web_ipv4sock >= 0)
	{
		websocket_destroy(sock->web_ipv4sock);
		sock->web_ipv4sock = -1;
		sock->type &= ~NETTYPE_WEBSOCKET_IPV4;
	}
#endif

	if(sock->ipv6sock >= 0)
	{
		priv_net_close_socket(sock->ipv6sock);
		sock->ipv6sock = -1;
		sock->type &= ~NETTYPE_IPV6;
	}

#if defined(CONF_WEBSOCKETS)
	if(sock->web_ipv6sock >= 0)
	{
		websocket_destroy(sock->web_ipv6sock);
		sock->web_ipv6sock = -1;
		sock->type &= ~NETTYPE_WEBSOCKET_IPV6;
	}
#endif

	// TClient: tear down the SOCKS5 control connection (closing it ends the UDP association).
	if(sock->proxy_tcp >= 0)
	{
		priv_net_close_socket(sock->proxy_tcp);
		sock->proxy_tcp = -1;
	}
	sock->proxy_active = 0;

	free(sock);
}

static int priv_net_create_socket(int domain, int type, const NETADDR *bindaddr)
{
	int sock = socket(domain, type, 0);
	if(sock < 0)
	{
		log_error("net", "Failed to create socket with domain %d and type %d (%s)", domain, type, net_error_message().c_str());
		return -1;
	}

#if defined(CONF_FAMILY_UNIX)
	// On TCP sockets set SO_REUSEADDR to fix port rebind on restart
	if(domain == AF_INET && type == SOCK_STREAM)
	{
		int reuse_addr = 1;
		if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse_addr, sizeof(reuse_addr)) != 0)
		{
			log_error("net", "Setting SO_REUSEADDR failed with domain %d and type %d (%s)", domain, type, net_error_message().c_str());
		}
	}
#elif defined(CONF_FAMILY_WINDOWS)
	{
		// Ensure exclusive use of address, otherwise it's possible on Windows to bind to the same address and port with another socket.
		// See https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse (last update 06/14/2022)
		int exclusive_addr_use = 1;
		if(setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive_addr_use, sizeof(exclusive_addr_use)) != 0)
		{
			log_error("net", "Setting SO_EXCLUSIVEADDRUSE failed with domain %d and type %d (%s)", domain, type, net_error_message().c_str());
		}
	}
#endif

	// Set to IPv6-only if that's what we are creating, to ensure that dual-stack does not block the same IPv4 port.
#if defined(IPV6_V6ONLY)
	if(domain == AF_INET6)
	{
		int ipv6only = 1;
		if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&ipv6only, sizeof(ipv6only)) != 0)
		{
			log_error("net", "Setting IPV6_V6ONLY failed with domain %d and type %d (%s)", domain, type, net_error_message().c_str());
		}
	}
#endif

	sockaddr_storage addr;
	socklen_t addr_len;
	if(bindaddr->type == NETTYPE_IPV4)
	{
		netaddr_to_sockaddr_in(bindaddr, (sockaddr_in *)&addr);
		addr_len = sizeof(sockaddr_in);
	}
	else if(bindaddr->type == NETTYPE_IPV6)
	{
		netaddr_to_sockaddr_in6(bindaddr, (sockaddr_in6 *)&addr);
		addr_len = sizeof(sockaddr_in6);
	}
	else
	{
		dbg_assert_failed("socket type invalid: %d", type);
	}

	if(bind(sock, (sockaddr *)&addr, addr_len) != 0)
	{
		log_error("net", "Failed to bind socket with domain %d and type %d (%s)", domain, type, net_error_message().c_str());
		priv_net_close_socket(sock);
		return -1;
	}

	return sock;
}

// ------------------------------------------------------------------
// Proxy: SOCKS5 (RFC 1928, UDP ASSOCIATE) and Shadowsocks (AEAD) UDP relay (TClient)
// ------------------------------------------------------------------

enum
{
	SS_CIPHER_NONE = 0,
	SS_CIPHER_CHACHA20_POLY1305,
	SS_CIPHER_AES_256_GCM,
	SS_CIPHER_AES_128_GCM,
};

static struct
{
	int type;
	int enabled;
	NETADDR addr;
	char user[64];
	char pass[64];
	int ss_cipher;
	int ss_keylen;
	unsigned char ss_key[32];
} g_Proxy; // zero-initialized
static int g_ProxyStatus = NET_PROXY_OFF;
static bool g_ProxyFailedThisBatch = false;

static int ss_cipher_from_name(const char *pName, int *pKeyLen)
{
	if(str_comp_nocase(pName, "chacha20-ietf-poly1305") == 0)
	{
		*pKeyLen = 32;
		return SS_CIPHER_CHACHA20_POLY1305;
	}
	if(str_comp_nocase(pName, "aes-256-gcm") == 0)
	{
		*pKeyLen = 32;
		return SS_CIPHER_AES_256_GCM;
	}
	if(str_comp_nocase(pName, "aes-128-gcm") == 0)
	{
		*pKeyLen = 16;
		return SS_CIPHER_AES_128_GCM;
	}
	*pKeyLen = 0;
	return SS_CIPHER_NONE;
}

#if defined(CONF_OPENSSL)
// Derives the Shadowsocks master key from the password (legacy OpenSSL EVP_BytesToKey
// with MD5, no salt, single iteration).
static void ss_derive_key(const char *pPass, unsigned char *pKey, int KeyLen)
{
	const int PassLen = str_length(pPass);
	unsigned char aPrev[16];
	int PrevLen = 0;
	int Produced = 0;
	while(Produced < KeyLen)
	{
		EVP_MD_CTX *pCtx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(pCtx, EVP_md5(), nullptr);
		if(PrevLen > 0)
			EVP_DigestUpdate(pCtx, aPrev, PrevLen);
		EVP_DigestUpdate(pCtx, pPass, PassLen);
		unsigned int DigestLen = 0;
		EVP_DigestFinal_ex(pCtx, aPrev, &DigestLen);
		EVP_MD_CTX_free(pCtx);
		PrevLen = 16;
		const int Copy = (KeyLen - Produced < 16) ? (KeyLen - Produced) : 16;
		mem_copy(pKey + Produced, aPrev, Copy);
		Produced += Copy;
	}
}

// HKDF-SHA1 subkey derivation with the fixed Shadowsocks "ss-subkey" info string.
static int ss_hkdf_sha1(const unsigned char *pKey, int KeyLen, const unsigned char *pSalt, int SaltLen, unsigned char *pOut, int OutLen)
{
	EVP_PKEY_CTX *pCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
	if(pCtx == nullptr)
		return -1;
	int Ok = 0;
	do
	{
		if(EVP_PKEY_derive_init(pCtx) <= 0)
			break;
		if(EVP_PKEY_CTX_set_hkdf_md(pCtx, EVP_sha1()) <= 0)
			break;
		if(EVP_PKEY_CTX_set1_hkdf_salt(pCtx, pSalt, SaltLen) <= 0)
			break;
		if(EVP_PKEY_CTX_set1_hkdf_key(pCtx, pKey, KeyLen) <= 0)
			break;
		if(EVP_PKEY_CTX_add1_hkdf_info(pCtx, (const unsigned char *)"ss-subkey", 9) <= 0)
			break;
		size_t Len = OutLen;
		if(EVP_PKEY_derive(pCtx, pOut, &Len) <= 0)
			break;
		Ok = (int)Len == OutLen;
	} while(false);
	EVP_PKEY_CTX_free(pCtx);
	return Ok ? 0 : -1;
}

static const EVP_CIPHER *ss_evp_cipher(int Cipher)
{
	switch(Cipher)
	{
	case SS_CIPHER_CHACHA20_POLY1305: return EVP_chacha20_poly1305();
	case SS_CIPHER_AES_256_GCM: return EVP_aes_256_gcm();
	case SS_CIPHER_AES_128_GCM: return EVP_aes_128_gcm();
	default: return nullptr;
	}
}

// AEAD seal: writes ciphertext||tag(16) to pOut, returns total length or -1.
static int ss_aead_seal(int Cipher, const unsigned char *pSubkey, const unsigned char *pNonce, const unsigned char *pPlain, int PlainLen, unsigned char *pOut)
{
	const EVP_CIPHER *pCipher = ss_evp_cipher(Cipher);
	if(pCipher == nullptr)
		return -1;
	EVP_CIPHER_CTX *pCtx = EVP_CIPHER_CTX_new();
	if(pCtx == nullptr)
		return -1;
	int OutLen = -1;
	do
	{
		if(EVP_EncryptInit_ex(pCtx, pCipher, nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) != 1)
			break;
		if(EVP_EncryptInit_ex(pCtx, nullptr, nullptr, pSubkey, pNonce) != 1)
			break;
		int Len = 0;
		if(EVP_EncryptUpdate(pCtx, pOut, &Len, pPlain, PlainLen) != 1)
			break;
		int Total = Len;
		if(EVP_EncryptFinal_ex(pCtx, pOut + Total, &Len) != 1)
			break;
		Total += Len;
		if(EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_GET_TAG, 16, pOut + Total) != 1)
			break;
		OutLen = Total + 16;
	} while(false);
	EVP_CIPHER_CTX_free(pCtx);
	return OutLen;
}

// AEAD open: pCipherData is ciphertext||tag(16); writes plaintext to pOut, returns length or -1.
static int ss_aead_open(int Cipher, const unsigned char *pSubkey, const unsigned char *pNonce, const unsigned char *pCipherData, int CipherLen, unsigned char *pOut)
{
	if(CipherLen < 16)
		return -1;
	const EVP_CIPHER *pCipher = ss_evp_cipher(Cipher);
	if(pCipher == nullptr)
		return -1;
	const int CtLen = CipherLen - 16;
	EVP_CIPHER_CTX *pCtx = EVP_CIPHER_CTX_new();
	if(pCtx == nullptr)
		return -1;
	int OutLen = -1;
	do
	{
		if(EVP_DecryptInit_ex(pCtx, pCipher, nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) != 1)
			break;
		if(EVP_DecryptInit_ex(pCtx, nullptr, nullptr, pSubkey, pNonce) != 1)
			break;
		int Len = 0;
		if(EVP_DecryptUpdate(pCtx, pOut, &Len, pCipherData, CtLen) != 1)
			break;
		int Total = Len;
		unsigned char aTag[16];
		mem_copy(aTag, pCipherData + CtLen, 16);
		if(EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_SET_TAG, 16, aTag) != 1)
			break;
		if(EVP_DecryptFinal_ex(pCtx, pOut + Total, &Len) != 1)
			break; // authentication failed
		OutLen = Total + Len;
	} while(false);
	EVP_CIPHER_CTX_free(pCtx);
	return OutLen;
}
#endif

void net_proxy_set(int type, int enabled, const NETADDR *proxy_addr, const char *username, const char *password, const char *method)
{
	g_Proxy.type = type;
	g_Proxy.enabled = enabled && proxy_addr != nullptr && proxy_addr->type != NETTYPE_INVALID;
	g_Proxy.addr = (proxy_addr != nullptr) ? *proxy_addr : NETADDR_ZEROED;
	str_copy(g_Proxy.user, username != nullptr ? username : "", sizeof(g_Proxy.user));
	str_copy(g_Proxy.pass, password != nullptr ? password : "", sizeof(g_Proxy.pass));
	g_Proxy.ss_cipher = SS_CIPHER_NONE;
	g_Proxy.ss_keylen = 0;
	mem_zero(g_Proxy.ss_key, sizeof(g_Proxy.ss_key));

	if(g_Proxy.enabled && type == NET_PROXY_TYPE_SHADOWSOCKS)
	{
		int KeyLen = 0;
		const int Cipher = ss_cipher_from_name(method != nullptr ? method : "", &KeyLen);
		if(Cipher == SS_CIPHER_NONE)
		{
			log_error("shadowsocks", "unknown cipher '%s' (use chacha20-ietf-poly1305, aes-256-gcm or aes-128-gcm)", method != nullptr ? method : "");
		}
		else
		{
#if defined(CONF_OPENSSL)
			g_Proxy.ss_cipher = Cipher;
			g_Proxy.ss_keylen = KeyLen;
			ss_derive_key(g_Proxy.pass, g_Proxy.ss_key, KeyLen);
#else
			log_error("shadowsocks", "this build has no OpenSSL, Shadowsocks is unavailable");
#endif
		}
	}

	// Start a fresh batch: the next sockets created will flip the status to ACTIVE/FAILED.
	g_ProxyFailedThisBatch = false;
	g_ProxyStatus = g_Proxy.enabled ? NET_PROXY_FAILED : NET_PROXY_OFF;
}

int net_proxy_status()
{
	return g_ProxyStatus;
}

int net_tcp_ping(const NETADDR *addr, int timeout_ms)
{
	if(addr == nullptr || addr->type == NETTYPE_INVALID)
		return -1;
	const int Family = (addr->type & NETTYPE_IPV6) ? AF_INET6 : AF_INET;
	const int Sock = socket(Family, SOCK_STREAM, 0);
	if(Sock < 0)
		return -1;

	// Non-blocking connect so we can time it with our own timeout.
	{
		unsigned long Mode = 1;
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(Sock, FIONBIO, &Mode);
#else
		ioctl(Sock, FIONBIO, &Mode);
#endif
	}

	const auto Start = std::chrono::steady_clock::now();
	int ConnectRes;
	if(Family == AF_INET6)
	{
		sockaddr_in6 sa;
		netaddr_to_sockaddr_in6(addr, &sa);
		ConnectRes = connect(Sock, (sockaddr *)&sa, sizeof(sa));
	}
	else
	{
		sockaddr_in sa;
		netaddr_to_sockaddr_in(addr, &sa);
		ConnectRes = connect(Sock, (sockaddr *)&sa, sizeof(sa));
	}

	int Ping = -1;
	if(ConnectRes == 0)
	{
		Ping = 0;
	}
	else
	{
		fd_set WriteFds;
		FD_ZERO(&WriteFds);
		FD_SET(Sock, &WriteFds);
		timeval Tv;
		Tv.tv_sec = timeout_ms / 1000;
		Tv.tv_usec = (timeout_ms % 1000) * 1000;
		const int Sel = select(Sock + 1, nullptr, &WriteFds, nullptr, &Tv);
		if(Sel > 0 && FD_ISSET(Sock, &WriteFds))
		{
			int Err = 0;
			socklen_t Len = sizeof(Err);
			if(getsockopt(Sock, SOL_SOCKET, SO_ERROR, (char *)&Err, &Len) == 0 && Err == 0)
				Ping = 0;
		}
	}
	if(Ping == 0)
	{
		const auto Elapsed = std::chrono::steady_clock::now() - Start;
		Ping = (int)std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count();
	}

	priv_net_close_socket(Sock);
	return Ping;
}

static void priv_socks5_set_timeout(int sock, int ms)
{
#if defined(CONF_FAMILY_WINDOWS)
	DWORD tv = ms;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
	struct timeval tv;
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#endif
}

static int priv_socks5_send_all(int sock, const unsigned char *buf, int n)
{
	int sent = 0;
	while(sent < n)
	{
		int r = send(sock, (const char *)(buf + sent), n - sent, 0);
		if(r <= 0)
			return -1;
		sent += r;
	}
	return 0;
}

static int priv_socks5_recv_all(int sock, unsigned char *buf, int n)
{
	int got = 0;
	while(got < n)
	{
		int r = recv(sock, (char *)(buf + got), n - got, 0);
		if(r <= 0)
			return -1;
		got += r;
	}
	return 0;
}

// Performs the SOCKS5 greeting, optional username/password authentication and the
// UDP ASSOCIATE request on an already-connected control socket. On success the UDP
// relay endpoint is written to *pRelay. Returns 0 on success, -1 on failure.
static int priv_socks5_handshake(int tcp, NETADDR *pRelay)
{
	const bool HaveAuth = g_Proxy.user[0] != '\0';

	// Greeting: VER, NMETHODS, METHODS...
	unsigned char aGreeting[4];
	int GreetingLen = 0;
	aGreeting[GreetingLen++] = 0x05;
	if(HaveAuth)
	{
		aGreeting[GreetingLen++] = 0x02;
		aGreeting[GreetingLen++] = 0x00; // no authentication
		aGreeting[GreetingLen++] = 0x02; // username/password
	}
	else
	{
		aGreeting[GreetingLen++] = 0x01;
		aGreeting[GreetingLen++] = 0x00; // no authentication
	}
	if(priv_socks5_send_all(tcp, aGreeting, GreetingLen) != 0)
	{
		log_error("socks5", "failed to send greeting (%s)", net_error_message().c_str());
		return -1;
	}

	unsigned char aMethod[2];
	if(priv_socks5_recv_all(tcp, aMethod, sizeof(aMethod)) != 0)
	{
		log_error("socks5", "no method reply from proxy (timeout or closed)");
		return -1;
	}
	if(aMethod[0] != 0x05)
	{
		log_error("socks5", "proxy is not SOCKS5 (got version %d)", aMethod[0]);
		return -1;
	}
	if(aMethod[1] == 0xFF)
	{
		log_error("socks5", "proxy rejected all authentication methods (credentials required?)");
		return -1;
	}
	if(aMethod[1] == 0x02)
	{
		// Username/password authentication (RFC 1929)
		const int UserLen = str_length(g_Proxy.user);
		const int PassLen = str_length(g_Proxy.pass);
		if(UserLen > 255 || PassLen > 255)
		{
			log_error("socks5", "username/password too long");
			return -1;
		}
		unsigned char aAuth[3 + 255 + 255];
		int AuthLen = 0;
		aAuth[AuthLen++] = 0x01; // subnegotiation version
		aAuth[AuthLen++] = (unsigned char)UserLen;
		mem_copy(aAuth + AuthLen, g_Proxy.user, UserLen);
		AuthLen += UserLen;
		aAuth[AuthLen++] = (unsigned char)PassLen;
		mem_copy(aAuth + AuthLen, g_Proxy.pass, PassLen);
		AuthLen += PassLen;
		if(priv_socks5_send_all(tcp, aAuth, AuthLen) != 0)
		{
			log_error("socks5", "failed to send credentials (%s)", net_error_message().c_str());
			return -1;
		}
		unsigned char aAuthReply[2];
		if(priv_socks5_recv_all(tcp, aAuthReply, sizeof(aAuthReply)) != 0 || aAuthReply[1] != 0x00)
		{
			log_error("socks5", "authentication failed (wrong username/password?)");
			return -1;
		}
	}
	else if(aMethod[1] != 0x00)
	{
		log_error("socks5", "proxy selected unsupported authentication method %d", aMethod[1]);
		return -1;
	}

	// UDP ASSOCIATE request. We do not know our source address yet, so send 0.0.0.0:0.
	const unsigned char aRequest[10] = {0x05, 0x03, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
	if(priv_socks5_send_all(tcp, aRequest, sizeof(aRequest)) != 0)
	{
		log_error("socks5", "failed to send UDP ASSOCIATE request (%s)", net_error_message().c_str());
		return -1;
	}

	unsigned char aHead[4];
	if(priv_socks5_recv_all(tcp, aHead, sizeof(aHead)) != 0)
	{
		log_error("socks5", "no UDP ASSOCIATE reply from proxy");
		return -1;
	}
	if(aHead[0] != 0x05 || aHead[1] != 0x00)
	{
		log_error("socks5", "proxy rejected UDP ASSOCIATE (reply code %d) - it likely does not support UDP", aHead[1]);
		return -1;
	}

	*pRelay = NETADDR_ZEROED;
	if(aHead[3] == 0x01) // IPv4
	{
		unsigned char aAddr[4 + 2];
		if(priv_socks5_recv_all(tcp, aAddr, sizeof(aAddr)) != 0)
			return -1;
		pRelay->type = NETTYPE_IPV4;
		mem_copy(pRelay->ip, aAddr, 4);
		pRelay->port = (aAddr[4] << 8) | aAddr[5];
	}
	else if(aHead[3] == 0x04) // IPv6
	{
		unsigned char aAddr[16 + 2];
		if(priv_socks5_recv_all(tcp, aAddr, sizeof(aAddr)) != 0)
			return -1;
		pRelay->type = NETTYPE_IPV6;
		mem_copy(pRelay->ip, aAddr, 16);
		pRelay->port = (aAddr[16] << 8) | aAddr[17];
	}
	else
	{
		log_error("socks5", "proxy returned an unsupported relay address type %d", aHead[3]);
		return -1;
	}

	// Many proxies report 0.0.0.0 as relay address, meaning "use the proxy's own IP".
	if(pRelay->type == NETTYPE_IPV4 && pRelay->ip[0] == 0 && pRelay->ip[1] == 0 && pRelay->ip[2] == 0 && pRelay->ip[3] == 0 && (g_Proxy.addr.type & NETTYPE_IPV4))
	{
		mem_copy(pRelay->ip, g_Proxy.addr.ip, 4);
	}
	return 0;
}

// Establishes the proxy relay for the given socket. On failure the socket keeps working
// without a proxy (traffic goes direct) and the status is FAILED.
static void priv_net_proxy_setup(NETSOCKET sock)
{
	sock->proxy_active = 0;
	sock->proxy_type = g_Proxy.type;
	sock->proxy_tcp = -1;

	if(!g_Proxy.enabled)
		return;
	// If a previous socket in this batch already failed (e.g. proxy unreachable), skip the
	// remaining ones immediately so we don't block for one timeout per socket.
	if(g_ProxyFailedThisBatch)
		return;

	// Shadowsocks needs no handshake: each datagram is encrypted and sent to the SS server.
	if(g_Proxy.type == NET_PROXY_TYPE_SHADOWSOCKS)
	{
#if defined(CONF_OPENSSL)
		if(g_Proxy.ss_cipher == SS_CIPHER_NONE)
		{
			g_ProxyFailedThisBatch = true;
			return;
		}
		sock->proxy_relay = g_Proxy.addr;
		sock->ss_cipher = g_Proxy.ss_cipher;
		sock->ss_keylen = g_Proxy.ss_keylen;
		mem_copy(sock->ss_key, g_Proxy.ss_key, sizeof(sock->ss_key));
		sock->proxy_active = 1;
		g_ProxyStatus = NET_PROXY_ACTIVE;
		char aRelay[NETADDR_MAXSTRSIZE];
		net_addr_str(&sock->proxy_relay, aRelay, sizeof(aRelay), true);
		log_info("shadowsocks", "relaying through %s", aRelay);
#else
		g_ProxyFailedThisBatch = true;
#endif
		return;
	}

	const NETADDR *pProxy = &g_Proxy.addr;
	const int Family = (pProxy->type & NETTYPE_IPV6) ? AF_INET6 : AF_INET;
	const int Tcp = socket(Family, SOCK_STREAM, 0);
	if(Tcp < 0)
	{
		log_error("socks5", "could not create control socket (%s)", net_error_message().c_str());
		g_ProxyFailedThisBatch = true;
		return;
	}
	priv_socks5_set_timeout(Tcp, 4000);

	int ConnectRes;
	if(Family == AF_INET6)
	{
		sockaddr_in6 sa;
		netaddr_to_sockaddr_in6(pProxy, &sa);
		ConnectRes = connect(Tcp, (sockaddr *)&sa, sizeof(sa));
	}
	else
	{
		sockaddr_in sa;
		netaddr_to_sockaddr_in(pProxy, &sa);
		ConnectRes = connect(Tcp, (sockaddr *)&sa, sizeof(sa));
	}
	if(ConnectRes != 0)
	{
		log_error("socks5", "could not connect to proxy (%s)", net_error_message().c_str());
		priv_net_close_socket(Tcp);
		g_ProxyFailedThisBatch = true;
		return;
	}

	NETADDR Relay;
	if(priv_socks5_handshake(Tcp, &Relay) != 0)
	{
		priv_net_close_socket(Tcp);
		g_ProxyFailedThisBatch = true;
		return;
	}

	sock->proxy_tcp = Tcp;
	sock->proxy_relay = Relay;
	sock->proxy_active = 1;
	g_ProxyStatus = NET_PROXY_ACTIVE;

	char aRelay[NETADDR_MAXSTRSIZE];
	net_addr_str(&Relay, aRelay, sizeof(aRelay), true);
	log_info("socks5", "UDP ASSOCIATE established, relaying through %s", aRelay);
}

// Wraps a UDP datagram in a SOCKS5 request header and sends it to the relay.
// Returns the number of payload bytes accepted, or -1 on error.
static int priv_net_socks5_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	unsigned char aBuf[4 + 16 + 2 + PACKETSIZE];
	int Header = 0;
	aBuf[Header++] = 0x00; // RSV
	aBuf[Header++] = 0x00; // RSV
	aBuf[Header++] = 0x00; // FRAG
	if(addr->type & NETTYPE_IPV4)
	{
		aBuf[Header++] = 0x01;
		mem_copy(aBuf + Header, addr->ip, 4);
		Header += 4;
	}
	else // NETTYPE_IPV6
	{
		aBuf[Header++] = 0x04;
		mem_copy(aBuf + Header, addr->ip, 16);
		Header += 16;
	}
	aBuf[Header++] = (addr->port >> 8) & 0xff;
	aBuf[Header++] = addr->port & 0xff;
	if(size < 0 || Header + size > (int)sizeof(aBuf))
		return -1;
	mem_copy(aBuf + Header, data, size);

	const NETADDR *pRelay = &sock->proxy_relay;
	int d = -1;
	if(pRelay->type & NETTYPE_IPV4)
	{
		if(sock->ipv4sock < 0)
			return -1;
		sockaddr_in sa;
		netaddr_to_sockaddr_in(pRelay, &sa);
		d = sendto(sock->ipv4sock, (const char *)aBuf, Header + size, 0, (sockaddr *)&sa, sizeof(sa));
	}
	else if(pRelay->type & NETTYPE_IPV6)
	{
		if(sock->ipv6sock < 0)
			return -1;
		sockaddr_in6 sa;
		netaddr_to_sockaddr_in6(pRelay, &sa);
		d = sendto(sock->ipv6sock, (const char *)aBuf, Header + size, 0, (sockaddr *)&sa, sizeof(sa));
	}
	if(d < 0)
		return -1;
	return size;
}

// Strips the SOCKS5 request header from a datagram received from the relay,
// rewriting *addr to the real source and advancing *data. Returns the new payload
// length, the unchanged length if the packet is not from the relay, or -1 to drop.
static int priv_net_socks5_udp_unwrap(NETSOCKET sock, NETADDR *addr, unsigned char **data, int bytes)
{
	if(!sock->proxy_active || net_addr_comp(addr, &sock->proxy_relay) != 0)
		return bytes;

	unsigned char *p = *data;
	if(bytes < 4 || p[2] != 0x00) // too short or fragmented
		return -1;
	const int Atyp = p[3];
	int Offset = 4;
	NETADDR Source = NETADDR_ZEROED;
	if(Atyp == 0x01) // IPv4
	{
		if(bytes < Offset + 4 + 2)
			return -1;
		Source.type = NETTYPE_IPV4;
		mem_copy(Source.ip, p + Offset, 4);
		Offset += 4;
	}
	else if(Atyp == 0x04) // IPv6
	{
		if(bytes < Offset + 16 + 2)
			return -1;
		Source.type = NETTYPE_IPV6;
		mem_copy(Source.ip, p + Offset, 16);
		Offset += 16;
	}
	else
	{
		return -1; // domain source cannot be represented as a NETADDR
	}
	Source.port = (p[Offset] << 8) | p[Offset + 1];
	Offset += 2;
	*addr = Source;
	*data = p + Offset;
	return bytes - Offset;
}

// Encrypts a UDP datagram as a Shadowsocks AEAD packet (salt || AEAD(addr+port+payload))
// and sends it to the Shadowsocks server. Returns the payload size, or -1 on error.
static int priv_net_ss_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
#if defined(CONF_OPENSSL)
	// plaintext = [ATYP][addr][port][payload]
	unsigned char aPlain[1 + 16 + 2 + PACKETSIZE];
	int Header = 0;
	if(addr->type & NETTYPE_IPV4)
	{
		aPlain[Header++] = 0x01;
		mem_copy(aPlain + Header, addr->ip, 4);
		Header += 4;
	}
	else // NETTYPE_IPV6
	{
		aPlain[Header++] = 0x04;
		mem_copy(aPlain + Header, addr->ip, 16);
		Header += 16;
	}
	aPlain[Header++] = (addr->port >> 8) & 0xff;
	aPlain[Header++] = addr->port & 0xff;
	if(size < 0 || Header + size > (int)sizeof(aPlain))
		return -1;
	mem_copy(aPlain + Header, data, size);
	const int PlainLen = Header + size;

	const int KeyLen = sock->ss_keylen;
	unsigned char aPacket[32 + sizeof(aPlain) + 16];
	if(RAND_bytes(aPacket, KeyLen) != 1)
		return -1;
	unsigned char aSubkey[32];
	if(ss_hkdf_sha1(sock->ss_key, KeyLen, aPacket, KeyLen, aSubkey, KeyLen) != 0)
		return -1;
	unsigned char aNonce[12];
	mem_zero(aNonce, sizeof(aNonce));
	const int Sealed = ss_aead_seal(sock->ss_cipher, aSubkey, aNonce, aPlain, PlainLen, aPacket + KeyLen);
	if(Sealed < 0)
		return -1;
	const int PacketLen = KeyLen + Sealed;

	const NETADDR *pRelay = &sock->proxy_relay;
	int d = -1;
	if(pRelay->type & NETTYPE_IPV4)
	{
		if(sock->ipv4sock < 0)
			return -1;
		sockaddr_in sa;
		netaddr_to_sockaddr_in(pRelay, &sa);
		d = sendto(sock->ipv4sock, (const char *)aPacket, PacketLen, 0, (sockaddr *)&sa, sizeof(sa));
	}
	else if(pRelay->type & NETTYPE_IPV6)
	{
		if(sock->ipv6sock < 0)
			return -1;
		sockaddr_in6 sa;
		netaddr_to_sockaddr_in6(pRelay, &sa);
		d = sendto(sock->ipv6sock, (const char *)aPacket, PacketLen, 0, (sockaddr *)&sa, sizeof(sa));
	}
	if(d < 0)
		return -1;
	return size;
#else
	(void)sock;
	(void)addr;
	(void)data;
	(void)size;
	return -1;
#endif
}

// Decrypts a Shadowsocks AEAD datagram from the server into sock->proxy_scratch,
// rewriting *addr to the real source and pointing *data into the scratch buffer.
static int priv_net_ss_udp_unwrap(NETSOCKET sock, NETADDR *addr, unsigned char **data, int bytes)
{
#if defined(CONF_OPENSSL)
	if(net_addr_comp(addr, &sock->proxy_relay) != 0)
		return bytes; // not from the Shadowsocks server, leave untouched

	const int KeyLen = sock->ss_keylen;
	if(bytes < KeyLen + 16)
		return -1;
	const unsigned char *pSalt = *data;
	unsigned char aSubkey[32];
	if(ss_hkdf_sha1(sock->ss_key, KeyLen, pSalt, KeyLen, aSubkey, KeyLen) != 0)
		return -1;
	unsigned char aNonce[12];
	mem_zero(aNonce, sizeof(aNonce));
	const int PlainLen = ss_aead_open(sock->ss_cipher, aSubkey, aNonce, *data + KeyLen, bytes - KeyLen, sock->proxy_scratch);
	if(PlainLen < 1)
		return -1;

	unsigned char *p = sock->proxy_scratch;
	const int Atyp = p[0];
	int Offset = 1;
	NETADDR Source = NETADDR_ZEROED;
	if(Atyp == 0x01) // IPv4
	{
		if(PlainLen < Offset + 4 + 2)
			return -1;
		Source.type = NETTYPE_IPV4;
		mem_copy(Source.ip, p + Offset, 4);
		Offset += 4;
	}
	else if(Atyp == 0x04) // IPv6
	{
		if(PlainLen < Offset + 16 + 2)
			return -1;
		Source.type = NETTYPE_IPV6;
		mem_copy(Source.ip, p + Offset, 16);
		Offset += 16;
	}
	else
	{
		return -1;
	}
	Source.port = (p[Offset] << 8) | p[Offset + 1];
	Offset += 2;
	*addr = Source;
	*data = sock->proxy_scratch + Offset;
	return PlainLen - Offset;
#else
	(void)sock;
	(void)addr;
	(void)data;
	(void)bytes;
	return -1;
#endif
}

// Dispatches an outgoing datagram to the active proxy backend.
static int priv_net_proxy_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	if(sock->proxy_type == NET_PROXY_TYPE_SHADOWSOCKS)
		return priv_net_ss_udp_send(sock, addr, data, size);
	return priv_net_socks5_udp_send(sock, addr, data, size);
}

// Dispatches an incoming datagram to the active proxy backend for unwrapping.
static int priv_net_proxy_udp_unwrap(NETSOCKET sock, NETADDR *addr, unsigned char **data, int bytes)
{
	if(sock->proxy_type == NET_PROXY_TYPE_SHADOWSOCKS)
		return priv_net_ss_udp_unwrap(sock, addr, data, bytes);
	return priv_net_socks5_udp_unwrap(sock, addr, data, bytes);
}

NETSOCKET net_udp_create(NETADDR bindaddr)
{
	NETSOCKET sock = (NETSOCKET_INTERNAL *)malloc(sizeof(*sock));
	*sock = invalid_socket;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		NETADDR bindaddr_ipv4 = bindaddr;
		bindaddr_ipv4.type = NETTYPE_IPV4;
		const int socket = priv_net_create_socket(AF_INET, SOCK_DGRAM, &bindaddr_ipv4);
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV4;
			sock->ipv4sock = socket;

			// Set broadcast
			{
				int broadcast = 1;
				if(setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) != 0)
				{
					log_error("net", "Setting SO_BROADCAST on IPv4 failed (%s)", net_error_message().c_str());
				}
			}

			// Set DSCP/TOS
			{
				int iptos = 0x10; // IPTOS_LOWDELAY
				if(setsockopt(socket, IPPROTO_IP, IP_TOS, (const char *)&iptos, sizeof(iptos)) != 0)
				{
					log_error("net", "Setting IP_TOS on IPv4 failed (%s)", net_error_message().c_str());
				}
			}
		}
	}

#if defined(CONF_WEBSOCKETS)
	if(bindaddr.type & NETTYPE_WEBSOCKET_IPV4)
	{
		NETADDR bindaddr_websocket_ipv4 = bindaddr;
		bindaddr_websocket_ipv4.type = NETTYPE_WEBSOCKET_IPV4;
		const int socket = websocket_create(&bindaddr_websocket_ipv4);
		if(socket >= 0)
		{
			sock->type |= NETTYPE_WEBSOCKET_IPV4;
			sock->web_ipv4sock = socket;
		}
	}
#endif

	if(bindaddr.type & NETTYPE_IPV6)
	{
		NETADDR bindaddr_ipv6 = bindaddr;
		bindaddr_ipv6.type = NETTYPE_IPV6;
		const int socket = priv_net_create_socket(AF_INET6, SOCK_DGRAM, &bindaddr_ipv6);
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV6;
			sock->ipv6sock = socket;

			// Set broadcast
			{
				int broadcast = 1;
				if(setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) != 0)
				{
					log_error("net", "Setting SO_BROADCAST on IPv6 failed (%s)", net_error_message().c_str());
				}
			}

			// Set DSCP/TOS
			// TODO: setting IP_TOS on ipv6 with setsockopt is not supported on Windows, see https://github.com/ddnet/ddnet/issues/7605
#if !defined(CONF_FAMILY_WINDOWS)
			{
				int iptos = 0x10; // IPTOS_LOWDELAY
				if(setsockopt(socket, IPPROTO_IP, IP_TOS, (const char *)&iptos, sizeof(iptos)) != 0)
				{
					log_error("net", "Setting IP_TOS on IPv6 failed (%s)", net_error_message().c_str());
				}
			}
#endif
		}
	}

#if defined(CONF_WEBSOCKETS)
	if(bindaddr.type & NETTYPE_WEBSOCKET_IPV6)
	{
		NETADDR bindaddr_websocket_ipv6 = bindaddr;
		bindaddr_websocket_ipv6.type = NETTYPE_WEBSOCKET_IPV6;
		const int socket = websocket_create(&bindaddr_websocket_ipv6);
		if(socket >= 0)
		{
			sock->type |= NETTYPE_WEBSOCKET_IPV6;
			sock->web_ipv6sock = socket;
		}
	}
#endif

	if(sock->type == NETTYPE_INVALID)
	{
		free(sock);
		sock = nullptr;
	}
	else
	{
		net_set_non_blocking(sock);
		net_buffer_init(&sock->buffer);
		// TClient: open the SOCKS5 UDP association (no-op when no proxy is configured).
		priv_net_proxy_setup(sock);
	}

	return sock;
}

int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	int d = -1;

	// TClient: relay regular UDP traffic through the SOCKS5 proxy. Broadcasts (LAN
	// discovery) and websocket traffic keep going direct.
	if(sock->proxy_active && (addr->type & (NETTYPE_IPV4 | NETTYPE_IPV6)) && !(addr->type & NETTYPE_LINK_BROADCAST))
	{
		d = priv_net_proxy_udp_send(sock, addr, data, size);
		if(d >= 0)
		{
			network_stats.sent_bytes += size;
			network_stats.sent_packets++;
		}
		return d;
	}

	if(addr->type & NETTYPE_IPV4)
	{
		if(sock->ipv4sock >= 0)
		{
			sockaddr_in sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin_port = htons(addr->port);
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = INADDR_BROADCAST;
			}
			else
			{
				netaddr_to_sockaddr_in(addr, &sa);
			}

			d = sendto(sock->ipv4sock, (const char *)data, size, 0, (sockaddr *)&sa, sizeof(sa));
		}
		else
		{
			log_error("net", "Cannot send IPv4 traffic to this socket");
		}
	}

#if defined(CONF_WEBSOCKETS)
	if(addr->type & NETTYPE_WEBSOCKET_IPV4)
	{
		if(sock->web_ipv4sock >= 0)
		{
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				log_error("net", "Cannot send broadcasts to Websocket IPv4");
			}
			else
			{
				d = websocket_send(sock->web_ipv4sock, (const unsigned char *)data, size, addr);
			}
		}
		else
		{
			log_error("net", "Cannot send Websocket IPv4 traffic to this socket");
		}
	}
#endif

	if(addr->type & NETTYPE_IPV6)
	{
		if(sock->ipv6sock >= 0)
		{
			sockaddr_in6 sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin6_port = htons(addr->port);
				sa.sin6_family = AF_INET6;
				sa.sin6_addr.s6_addr[0] = 0xff; /* multicast */
				sa.sin6_addr.s6_addr[1] = 0x02; /* link local scope */
				sa.sin6_addr.s6_addr[15] = 1; /* all nodes */
			}
			else
			{
				netaddr_to_sockaddr_in6(addr, &sa);
			}

			d = sendto(sock->ipv6sock, (const char *)data, size, 0, (sockaddr *)&sa, sizeof(sa));
		}
		else
		{
			log_error("net", "Cannot send IPv6 traffic to this socket");
		}
	}

#if defined(CONF_WEBSOCKETS)
	if(addr->type & NETTYPE_WEBSOCKET_IPV6)
	{
		if(sock->web_ipv6sock >= 0)
		{
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				log_error("net", "Cannot send broadcasts to Websocket IPv6");
			}
			else
			{
				d = websocket_send(sock->web_ipv6sock, (const unsigned char *)data, size, addr);
			}
		}
		else
		{
			log_error("net", "Cannot send Websocket IPv6 traffic to this socket");
		}
	}
#endif

	network_stats.sent_bytes += size;
	network_stats.sent_packets++;
	return d;
}

int net_udp_recv(NETSOCKET sock, NETADDR *addr, unsigned char **data)
{
	static const auto &&update_stats = [](int bytes) {
		network_stats.recv_bytes += bytes;
		network_stats.recv_packets++;
	};

	int bytes = 0;
#if defined(CONF_PLATFORM_LINUX)
	if(sock->ipv4sock >= 0)
	{
		if(sock->buffer.pos >= sock->buffer.size)
		{
			net_buffer_reinit(&sock->buffer);
			sock->buffer.size = recvmmsg(sock->ipv4sock, sock->buffer.msgs, VLEN, 0, NULL);
			sock->buffer.pos = 0;
		}
	}

	if(sock->ipv6sock >= 0)
	{
		if(sock->buffer.pos >= sock->buffer.size)
		{
			net_buffer_reinit(&sock->buffer);
			sock->buffer.size = recvmmsg(sock->ipv6sock, sock->buffer.msgs, VLEN, 0, NULL);
			sock->buffer.pos = 0;
		}
	}

	if(sock->buffer.pos < sock->buffer.size)
	{
		sockaddr_to_netaddr((sockaddr *)&(sock->buffer.sockaddrs[sock->buffer.pos]), sizeof(sock->buffer.sockaddrs[sock->buffer.pos]), addr);
		bytes = sock->buffer.msgs[sock->buffer.pos].msg_len;
		*data = (unsigned char *)sock->buffer.bufs[sock->buffer.pos];
		sock->buffer.pos++;
		if(sock->proxy_active)
		{
			bytes = priv_net_proxy_udp_unwrap(sock, addr, data, bytes);
			if(bytes < 0)
				return 0; // drop malformed relay datagram
		}
		update_stats(bytes);
		return bytes;
	}
#else
	if(sock->ipv4sock >= 0)
	{
		sockaddr_storage recv_addr;
		socklen_t fromlen = sizeof(recv_addr);
		bytes = recvfrom(sock->ipv4sock, sock->buffer.buf, sizeof(sock->buffer.buf), 0, (sockaddr *)&recv_addr, &fromlen);
		*data = (unsigned char *)sock->buffer.buf;
		if(bytes > 0)
		{
			sockaddr_to_netaddr((sockaddr *)&recv_addr, fromlen, addr);
			if(sock->proxy_active)
			{
				bytes = priv_net_proxy_udp_unwrap(sock, addr, data, bytes);
				if(bytes < 0)
					return 0; // drop malformed relay datagram
			}
			update_stats(bytes);
			return bytes;
		}
	}

	if(sock->ipv6sock >= 0)
	{
		sockaddr_storage recv_addr;
		socklen_t fromlen = sizeof(recv_addr);
		bytes = recvfrom(sock->ipv6sock, sock->buffer.buf, sizeof(sock->buffer.buf), 0, (sockaddr *)&recv_addr, &fromlen);
		*data = (unsigned char *)sock->buffer.buf;
		if(bytes > 0)
		{
			sockaddr_to_netaddr((sockaddr *)&recv_addr, fromlen, addr);
			if(sock->proxy_active)
			{
				bytes = priv_net_proxy_udp_unwrap(sock, addr, data, bytes);
				if(bytes < 0)
					return 0; // drop malformed relay datagram
			}
			update_stats(bytes);
			return bytes;
		}
	}
#endif

#if defined(CONF_WEBSOCKETS)
	if(sock->web_ipv4sock >= 0)
	{
		char *buf;
		int size;
		net_buffer_simple(&sock->buffer, &buf, &size);
		bytes = websocket_recv(sock->web_ipv4sock, (unsigned char *)buf, size, addr);
		*data = (unsigned char *)buf;
		if(bytes > 0)
		{
			update_stats(bytes);
			return bytes;
		}
	}

	if(sock->web_ipv6sock >= 0)
	{
		char *buf;
		int size;
		net_buffer_simple(&sock->buffer, &buf, &size);
		bytes = websocket_recv(sock->web_ipv6sock, (unsigned char *)buf, size, addr);
		*data = (unsigned char *)buf;
		if(bytes > 0)
		{
			update_stats(bytes);
			return bytes;
		}
	}
#endif

	return bytes < 0 ? -1 : 0;
}

void net_udp_close(NETSOCKET sock)
{
	priv_net_close_all_sockets(sock);
}

NETSOCKET net_tcp_create(NETADDR bindaddr)
{
	NETSOCKET sock = (NETSOCKET_INTERNAL *)malloc(sizeof(*sock));
	*sock = invalid_socket;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		NETADDR bindaddr_ipv4 = bindaddr;
		bindaddr_ipv4.type = NETTYPE_IPV4;
		const int socket4 = priv_net_create_socket(AF_INET, SOCK_STREAM, &bindaddr_ipv4);
		if(socket4 >= 0)
		{
			sock->type |= NETTYPE_IPV4;
			sock->ipv4sock = socket4;
		}
	}

	if(bindaddr.type & NETTYPE_IPV6)
	{
		NETADDR bindaddr_ipv6 = bindaddr;
		bindaddr_ipv6.type = NETTYPE_IPV6;
		const int socket6 = priv_net_create_socket(AF_INET6, SOCK_STREAM, &bindaddr_ipv6);
		if(socket6 >= 0)
		{
			sock->type |= NETTYPE_IPV6;
			sock->ipv6sock = socket6;
		}
	}

	if(sock->type == NETTYPE_INVALID)
	{
		free(sock);
		sock = nullptr;
	}

	return sock;
}

int net_tcp_listen(NETSOCKET sock, int backlog)
{
	int err = -1;
	if(sock->ipv4sock >= 0)
	{
		err = listen(sock->ipv4sock, backlog);
	}
	if(sock->ipv6sock >= 0)
	{
		err = listen(sock->ipv6sock, backlog);
	}
	return err;
}

int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *a)
{
	*new_sock = nullptr;

	if(sock->ipv4sock >= 0)
	{
		sockaddr_storage addr;
		socklen_t sockaddr_len = sizeof(addr);

		int s = accept(sock->ipv4sock, (sockaddr *)&addr, &sockaddr_len);
		if(s != -1)
		{
			sockaddr_to_netaddr((sockaddr *)&addr, sockaddr_len, a);

			*new_sock = (NETSOCKET_INTERNAL *)malloc(sizeof(**new_sock));
			**new_sock = invalid_socket;
			(*new_sock)->type = NETTYPE_IPV4;
			(*new_sock)->ipv4sock = s;
			return s;
		}
	}

	if(sock->ipv6sock >= 0)
	{
		sockaddr_storage addr;
		socklen_t sockaddr_len = sizeof(addr);

		int s = accept(sock->ipv6sock, (sockaddr *)&addr, &sockaddr_len);
		if(s != -1)
		{
			*new_sock = (NETSOCKET_INTERNAL *)malloc(sizeof(**new_sock));
			**new_sock = invalid_socket;
			sockaddr_to_netaddr((sockaddr *)&addr, sockaddr_len, a);
			(*new_sock)->type = NETTYPE_IPV6;
			(*new_sock)->ipv6sock = s;
			return s;
		}
	}

	return -1;
}

int net_tcp_connect(NETSOCKET sock, const NETADDR *a)
{
	if(a->type & NETTYPE_IPV4)
	{
		if(sock->ipv4sock < 0)
			return -2;
		sockaddr_in addr;
		netaddr_to_sockaddr_in(a, &addr);
		return connect(sock->ipv4sock, (sockaddr *)&addr, sizeof(addr));
	}

	if(a->type & NETTYPE_IPV6)
	{
		if(sock->ipv6sock < 0)
			return -2;
		sockaddr_in6 addr;
		netaddr_to_sockaddr_in6(a, &addr);
		return connect(sock->ipv6sock, (sockaddr *)&addr, sizeof(addr));
	}

	return -1;
}

int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr)
{
	net_set_non_blocking(sock);
	int res = net_tcp_connect(sock, &bindaddr);
	net_set_blocking(sock);
	return res;
}

int net_tcp_send(NETSOCKET sock, const void *data, int size)
{
	int bytes = -1;

	if(sock->ipv4sock >= 0)
	{
		bytes = send(sock->ipv4sock, (const char *)data, size, 0);
	}
	if(sock->ipv6sock >= 0)
	{
		bytes = send(sock->ipv6sock, (const char *)data, size, 0);
	}

	return bytes;
}

int net_tcp_recv(NETSOCKET sock, void *data, int maxsize)
{
	int bytes = -1;

	if(sock->ipv4sock >= 0)
	{
		bytes = recv(sock->ipv4sock, (char *)data, maxsize, 0);
	}
	if(sock->ipv6sock >= 0)
	{
		bytes = recv(sock->ipv6sock, (char *)data, maxsize, 0);
	}

	return bytes;
}

void net_tcp_close(NETSOCKET sock)
{
	priv_net_close_all_sockets(sock);
}

#if defined(CONF_FAMILY_UNIX)
UNIXSOCKET net_unix_create_unnamed()
{
	return socket(AF_UNIX, SOCK_DGRAM, 0);
}

int net_unix_send(UNIXSOCKET sock, UNIXSOCKETADDR *addr, void *data, int size)
{
	return sendto(sock, data, size, 0, (sockaddr *)addr, sizeof(*addr));
}

void net_unix_set_addr(UNIXSOCKETADDR *addr, const char *path)
{
	mem_zero(addr, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	str_copy(addr->sun_path, path);
}

void net_unix_close(UNIXSOCKET sock)
{
	close(sock);
}
#endif
