/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * main.c - Network shell for psplink
 *
 * Copyright (c) 2005 James F <tyranid@gmail.com>
 * Some small parts (c) 2005 PSPPet
 *
 * $Id$
 * $HeadURL$
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspsdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <pspiofilemgr.h>
#include <netinet/tcp.h>

int modNetIsInit(void);
const char* modNetGetIpAddress(void);

#define MODULE_NAME "NetShell"
#define WELCOME_MESSAGE "Welcome to PSPLink's NetShell\n"

PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_MAIN_THREAD_NAME("NetShell");

#define printf pspDebugScreenPrintf

#define socket sceNetInetSocket
#define bind sceNetInetBind
#define listen sceNetInetListen
#define accept sceNetInetAccept
#define recv sceNetInetRecv
#define send sceNetInetSend
#define close sceNetInetClose
#define setsockopt sceNetInetSetsockopt
#undef errno
#define errno sceNetInetGetErrno()

void *memcpy(void *dst, const void *src, unsigned int cnt){
	for(int i = 0;i < cnt;i++){
		((uint8_t*)dst)[i] = ((uint8_t*)src)[i];
	}
	return dst;
}

void *memset(void *dst, int val, unsigned int cnt){
	for(int i = 0;i < cnt;i++){
		((uint8_t*)dst)[i] = (uint8_t)val;
	}
	return dst;
}

char *inet_ntoa(struct in_addr in){
	static char buf[64];
	memset(buf, 0, sizeof(buf));
	sceNetInetInetNtop(AF_INET, &in.s_addr, buf, sizeof(buf));
	return buf;
}

int psplinkParseCommand(char *command);
void psplinkPrintPrompt(void);
void psplinkExitShell(void);
void ttySetWifiHandler(PspDebugPrintHandler wifiHandler);

int g_currsock = -1;
int g_servsock = -1;
unsigned int g_size = 0;
char g_data[32 * 1024];
int g_data_lock = 0;

#define SERVER_PORT 10000

static int (*kernel_delay_thread_function)(unsigned int delay) = NULL;

static int kernel_print_callback(const char *data, int size)
{
	if (g_currsock < 0){
		return size;
	}

	int total_wait_time = 0;
	while(g_data_lock)
	{
		if (total_wait_time > 100000){
			return size;
		}
		kernel_delay_thread_function(5000);
		total_wait_time += 5000;
	}
	g_data_lock = 1;

	if (size + g_size < sizeof(g_data)){
		memcpy(&g_data[g_size], data, size);
		g_size += size;
	}

	g_data_lock = 0;

	return size;
}

static int make_socket(uint16_t port)
{
	int sock;
	int ret;
	struct sockaddr_in name;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock < 0)
	{
		return -1;
	}

	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(sock, (struct sockaddr *) &name, sizeof(name));
	if(ret < 0)
	{
		return -1;
	}

	return sock;
}

static int send_thread_func(unsigned int args, void *argp){
	int fd = sceIoOpen("ms0:/netshell.txt", PSP_O_TRUNC | PSP_O_CREAT | PSP_O_WRONLY, 0777);
	if (fd >= 0){
		sceIoClose(fd);
	}
	while(1){
		if (g_currsock < 0){
			sceKernelDelayThread(100000);
			continue;
		}

		while (g_data_lock){
			sceKernelDelayThread(5000);
		}
		g_data_lock = 1;

		if (g_size == 0){
			g_data_lock = 0;
			sceKernelDelayThread(5000);
			continue;
		}

		int size = g_size > sizeof(g_data) ? sizeof(g_data) : g_size;
		static char data_copy[sizeof(g_data)];
		memcpy(data_copy, g_data, size);
		g_size = 0;
		g_data_lock = 0;

		fd = sceIoOpen("ms0:/netshell.txt", PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
		if (fd >= 0){
			sceIoWrite(fd, data_copy, size);
			sceIoClose(fd);
		}
		//pspDebugScreenPrintf("%s: starting send of %d bytes of data\n", __func__, size);
		int send_status = 0;
		#if 1
		while(1){
			send_status = send(g_currsock, data_copy, size, MSG_DONTWAIT);
			if (send_status == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
				sceKernelDelayThread(5000);
				continue;
			}
			break;
		}
		#else
		send_status = send(g_currsock, data_copy, size, 0);
		#endif
		// we are assuming that anything we send will just fit into send buffer here...
		if(send_status < 0)
		{
			pspDebugScreenPrintf("%s: send failed, %d\n", __func__, errno);
			int currsock = g_currsock;
			g_currsock = -1;
			close(currsock);
			g_size = 0;
			continue;
		}
		//pspDebugScreenPrintf("%s: send finished\n", __func__);

		sceKernelDelayThread(0);
	}
	return 0;
}

static int recv_thread_func(unsigned int args, void *argp){
	static char cli[1024] = {0};
	static int pos = 0;
	while(1){
		if (g_currsock < 0){
			pos = 0;
			sceKernelDelayThread(100000);
			continue;
		}

		char data;
		//pspDebugScreenPrintf("%s: starting data receive\n", __func__);
		#if 1
		int recv_status = recv(g_currsock, &data, 1, MSG_DONTWAIT);
		if (recv_status == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
			sceKernelDelayThread(100000);
			continue;
		}
		#else
		int recv_status = recv(g_currsock, &data, 1, 0);
		#endif
		//pspDebugScreenPrintf("%s: data received\n", __func__);
		if (recv_status != 1){
			if (recv_status == 0){
				pspDebugScreenPrintf("%s: remote closed the socket\n", __func__);
			}else{
				pspDebugScreenPrintf("%s: recv failed, %d, closing socket\n", __func__, errno);
			}
			int currsock = g_currsock;
			g_currsock = -1;
			close(currsock);
			continue;
		}

		if ((data == 10) || (data == 13))
		{

			if(pos > 0)
			{
				#if 0
				psplinkParseCommand("exprint");
				sceKernelDelayThread(100000);
				#endif

				cli[pos] = 0;
				//pspDebugScreenPrintf("%s: forwarding cli input [%s] to psplink\n", __func__, cli);
				pos = 0;
				int parse_status = psplinkParseCommand(cli);
				if(parse_status < 0)
				{
					pspDebugScreenPrintf("%s: cmd parsed failed, %d, terminating\n", __func__, parse_status);
					int servsock = g_servsock;
					int currsock = g_currsock;
					g_servsock = -1;
					g_currsock = -1;
					close(servsock);
					close(currsock);
					psplinkExitShell();
				}
				sceKernelDelayThread(100000);
			}
		}
		else if(pos < (sizeof(cli) -1))
		{
			cli[pos++] = data;
		}
	}

	return 0;
}


/* Start a simple tcp echo server */
static void start_server(const char *szIpAddr)
{
	int ret;
	int sock;
	struct sockaddr_in client;

	int tid = sceKernelCreateThread("netshell recv thread", recv_thread_func, 0x18, 8192, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);
	tid = sceKernelCreateThread("netshell send thread", send_thread_func, 0x18, 8192, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);

	ttySetWifiHandler(kernel_print_callback);

	/* Create a socket for listening */
	sock = make_socket(SERVER_PORT);
	if(sock < 0)
	{
		pspDebugScreenPrintf("Error creating server socket\n");
		return;
	}
	g_servsock = sock;

	ret = listen(sock, 1);
	if(ret < 0)
	{
		pspDebugScreenPrintf("Error calling listen\n");
		return;
	}

	pspDebugScreenPrintf("Listening for connections ip %s port %d\n", szIpAddr, SERVER_PORT);

	while(g_servsock >= 0)
	{
		socklen_t addr_size = sizeof(struct sockaddr_in);
		int new_sock = accept(sock, (struct sockaddr *) &client, &addr_size);
		if(new_sock < 0)
		{
			pspDebugScreenPrintf("Error in accept %d\n", errno);
			close(sock);
			g_servsock = -1;
			return;
		}

		int sockopt = 1024 * 1024;
		setsockopt(new_sock, SOL_SOCKET, SO_SNDBUF, &sockopt, sizeof(sockopt));
		sockopt = 1024 * 1024;
		setsockopt(new_sock, SOL_SOCKET, SO_RCVBUF, &sockopt, sizeof(sockopt));
		sockopt = 1;
		setsockopt(new_sock, IPPROTO_TCP, TCP_NODELAY, &sockopt, sizeof(sockopt));

		printf("New connection %d from %s:%d\n", new_sock,
				inet_ntoa(client.sin_addr),
				ntohs(client.sin_port));

		send(new_sock, WELCOME_MESSAGE, strlen(WELCOME_MESSAGE), 0);
		g_currsock = new_sock;
		psplinkPrintPrompt();

		while(g_currsock > 0){
			sceKernelDelayThread(4096);
		}
		pspDebugScreenPrintf("%s: current socket closed, listening for new connection\n", __func__);
	}

	close(sock);
	g_servsock = -1;
}

void *get_kernel_delay_thread_function();
/* Simple thread */
static int main_thread_func(unsigned int args, void *arg)
{
	pspDebugScreenPrintf("PSPLink NetShell (c) 2k6 TyRaNiD\n");

	kernel_delay_thread_function = get_kernel_delay_thread_function();
	pspDebugScreenPrintf("%s: kernel delay thread function is at 0x%lx\n", __func__, (uint32_t)kernel_delay_thread_function);

	if(modNetIsInit() >= 0)
	{
		const char *ip;
		ip = modNetGetIpAddress();
		start_server(ip);
	}

	return 0;
}

int module_start(SceSize args, void *argp){
	int tid = sceKernelCreateThread("netshell main", main_thread_func, 0x18, 8192, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	if(g_currsock >= 0)
	{
		close(g_currsock);
	}
	if(g_servsock >= 0)
	{
		close(g_servsock);
	}

	return 0;
}
