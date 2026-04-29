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

int modNetIsInit(void);
const char* modNetGetIpAddress(void);

#define MODULE_NAME "NetShell"
#define WELCOME_MESSAGE "Welcome to PSPLink's NetShell\n"

PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_MAIN_THREAD_NAME("NetShell");

#define printf pspDebugScreenPrintf

int psplinkParseCommand(char *command);
void psplinkPrintPrompt(void);
void psplinkExitShell(void);
void ttySetWifiHandler(PspDebugPrintHandler wifiHandler);

int g_currsock = -1;
int g_servsock = -1;
int g_size = 0;
char g_data[32 * 1024];
int g_data_lock = 0;

#define SERVER_PORT 10000

int wifiPrint(const char *data, int size)
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
		sceKernelDelayThread(5000);
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

int make_socket(uint16_t port)
{
	int sock;
	int ret;
	struct sockaddr_in name;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock < 0)
	{
		return -1;
	}

	int sockopt = 64 * 1024;
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sockopt, sizeof(sockopt));

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

int send_thread_func(unsigned int args, void *argp){
	int fd = sceIoOpen("ms0:/netshell.txt", PSP_O_TRUNC | PSP_O_CREAT | PSP_O_WRONLY, 0777);
	if (fd >= 0){
		sceIoClose(fd);
	}
	while(1){
		if (g_currsock < 0){
			g_size = 0;
			sceKernelDelayThread(100000);
			continue;
		}

		if (g_size == 0){
			sceKernelDelayThread(5000);
			continue;
		}

		while (g_data_lock){
			sceKernelDelayThread(5000);
		}
		g_data_lock = 1;

		fd = sceIoOpen("ms0:/netshell.txt", PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
		if (fd >= 0){
			sceIoWrite(fd, g_data, g_size);
			sceIoClose(fd);
		}
		//pspDebugScreenPrintf("%s: starting send of %d byte data [%s]\n", __func__, g_size, log_buf);
		int send_status = 0;
		#if 1
		while(1){
			send_status = send(g_currsock, g_data, g_size, MSG_DONTWAIT);
			if (send_status == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
				sceKernelDelayThread(5000);
				continue;
			}
			break;
		}
		#else
		send_status = send(g_currsock, g_data, g_size, 0);
		#endif
		//pspDebugScreenPrintf("%s: send finished\n", __func__);
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
		g_size = 0;
		g_data_lock = 0;

		sceKernelDelayThread(0);
	}
	return 0;
}

int recv_thread_func(unsigned int args, void *argp){
	char cli[1024];
	int pos = 0;
	while(1){
		if (g_currsock < 0){
			pos = 0;
			sceKernelDelayThread(100000);
			continue;
		}

		char data;
		#if 1
		int recv_status = recv(g_currsock, &data, 1, MSG_DONTWAIT);
		if (recv_status == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
			sceKernelDelayThread(100000);
			continue;
		}
		#else
		int recv_status = recv(g_currsock, &data, 1, 0);
		#endif
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
				psplinkParseCommand("exprint");
				sceKernelDelayThread(100000);
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
void start_server(const char *szIpAddr)
{
	int ret;
	int sock;
	struct sockaddr_in client;

	int tid = sceKernelCreateThread("netshell recv thread", recv_thread_func, 0x18, 8192, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);
	tid = sceKernelCreateThread("netshell send thread", send_thread_func, 0x18, 8192, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);

	ttySetWifiHandler(wifiPrint);

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
			pspDebugScreenPrintf("Error in accept %s\n", strerror(errno));
			close(sock);
			g_servsock = -1;
			return;
		}

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

/* Simple thread */
int main(int argc, char **argv)
{
	pspDebugScreenPrintf("PSPLink NetShell (c) 2k6 TyRaNiD\n");

	if(modNetIsInit() >= 0)
	{
		const char *ip;
		ip = modNetGetIpAddress();
		start_server(ip);
	}

	sceKernelSleepThread();

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
