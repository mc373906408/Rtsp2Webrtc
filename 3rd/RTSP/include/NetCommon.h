#ifndef __NETCOMMON_H__
#define __NETCOMMON_H__

#ifdef WIN32

#include <WinSock2.h>
#include <ws2tcpip.h>

#define closeSocket	closesocket
#define EWOULDBLOCK	WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EINTR		WSAEINTR

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define closeSocket			close
#define WSAGetLastError()	errno

#include <ctype.h>
#include <stdlib.h>
#endif

#endif
