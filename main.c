/*
 * This file is part of PRO ONLINE.
 *
 * PRO ONLINE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PRO ONLINE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO ONLINE. If not, see <http://www.gnu.org/licenses/ .
 */

#include <pspmoduleinfo.h>
#include <pspiofilemgr.h>
#include <psputils.h>
#include <psploadcore.h>
#include <pspsysmem_kernel.h>

#include <systemctrl.h>

#include <string.h>
#include <stdio.h>

#define LOG_PATH "ms0:/psplink_wifi_shim.log"
#define LOG(...){ \
	uint32_t k1 = pspSdkSetK1(0); \
	int _fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_APPEND | PSP_O_CREAT, 0777); \
	if (_fd > 0){ \
		char _log_buf[256]; \
		int _len = sprintf(_log_buf, __VA_ARGS__); \
		sceIoWrite(_fd, _log_buf, _len); \
		sceIoClose(_fd); \
	} \
	pspSdkSetK1(k1); \
}

#define LOG_INIT(...){ \
	int _fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_TRUNC | PSP_O_CREAT, 0777); \
	if (_fd > 0){ \
		sceIoClose(_fd); \
	} \
}

PSP_MODULE_INFO("psplink_wifi_shim", PSP_MODULE_KERNEL, 1, 0);

static int is_vita(){
	static int vita = -1;
	if (vita == -1){
		int test_file = sceIoOpen("flash0:/kd/usb.prx", PSP_O_RDONLY, 0777);
		if (test_file >= 0){
			sceIoClose(test_file);
			vita = 0;
		}else{
			vita = 1;
		}
	}
	return vita;
}

static int has_high_mem(){
	return is_vita() || sceKernelGetModel() != 0;
}

typedef struct PartitionData {
	u32 unk[5];
	u32 size;
} PartitionData;

typedef struct SysMemPartition {
	struct SysMemPartition *next;
	u32	address;
	u32 size;
	u32 attributes;
	PartitionData *data;
} SysMemPartition;

// based on Adrenaline's implementation
static void memlayout_hack(){
	if(!has_high_mem()){
		LOG("%s: not slim/vita\n", __func__);
		return;
	}

	SysMemPartition *(*get_partition)(int p) = NULL;
	for (u32 addr = 0x88000000;addr < 0x4000 + 0x88000000;addr+=4){
		if (_lw(addr) == 0x2C85000D){
			get_partition = (SysMemPartition *(*)(int p))(addr-4);
			break;
		}
	}

	if (get_partition == NULL){
		LOG("%s: can't find get_partition\n", __func__);
		return;
	}

	SysMemPartition *partition_2 = get_partition(2);
	SysMemPartition *partition_9 = get_partition(is_vita() ? 11 : 9);

	if (partition_9 == NULL){
		LOG("%s: partition 9 not found\n", __func__);
		return;
	}

	// limit to 16MB max as "mid memory layout"
	partition_2->size = 35 * 1024 * 1024;
	partition_9->size = (40 - 35) * 1024 * 1024;

	// complete other fields
	partition_2->data->size = (((partition_2->size >> 8) << 9) | 0xFC);
	partition_9->address = 0x08800000 + partition_2->size;
	partition_9->data->size = (((partition_9->size >> 8) << 9) | 0xFC);

	// allow full user access on extra partition
	partition_9->attributes = 0xf;

	// Change memory protection
	u32 *prot = (u32 *)0xBC000040;

	int i;
	for (i = 0; i < 0x10; i++) {
		prot[i] = 0xFFFFFFFF;
	}

	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheClearAll();

	LOG("%s: changed partition layout\n", __func__);
}

static void log_memory_info(){
	PspSysmemPartitionInfo meminfo = {0};
	meminfo.size = sizeof(PspSysmemPartitionInfo);
	for(int i = 1;i < 13;i++){
		int query_status = sceKernelQueryMemoryPartitionInfo(i, &meminfo);
		if (query_status == 0){
			int max_free = sceKernelPartitionMaxFreeMemSize(i);
			int total_free = sceKernelPartitionTotalFreeMemSize(i);
			LOG("%s: p%d startaddr 0x%x size %d attr 0x%x max %d total %d\n", __func__, i, meminfo.startaddr, meminfo.memsize, meminfo.attr, max_free, total_free);
		}else{
			LOG("%s: p%d query failed, 0x%x\n", __func__, i, query_status);
		}
	}
}

#define NOP 0
#define MAKE_JUMP(a, f) _sw(0x08000000 | (((u32)(f) & 0x0FFFFFFC) >> 2), a)
#define GET_JUMP_TARGET(x) (0x80000000 | (((x) & 0x03FFFFFF) << 2))
// Davee's new 5 bytes chainable trampoline used in ARK cfw and RJL fork
#define HIJACK_FUNCTION(a, f, p) \
{ \
	int _interrupts = pspSdkDisableInterrupts(); \
	static u32 _pb_[5]; \
	_sw(_lw((u32)(a)), (u32)_pb_); \
	_sw(_lw((u32)(a) + 4), (u32)_pb_ + 4);\
	_sw(NOP, (u32)_pb_ + 8);\
	_sw(NOP, (u32)_pb_ + 16);\
	MAKE_JUMP((u32)_pb_ + 12, (u32)(a) + 8); \
	_sw(0x08000000 | (((u32)(f) >> 2) & 0x03FFFFFF), (u32)(a)); \
	_sw(0, (u32)(a) + 4); \
	p = (void *)_pb_; \
	sceKernelDcacheWritebackInvalidateAll(); \
	sceKernelIcacheClearAll(); \
	pspSdkEnableInterrupts(_interrupts); \
}

static int partition_to_use(){
	return is_vita() ? 11 : 9;
}

static int (*load_module_orig)(const char *path, int flags, SceKernelLMOption *option) = NULL;
static int load_module(const char *path, int flags, SceKernelLMOption *option){
	if (strstr(path, "pspnet.prx") != NULL){
		// to put this in p9 it needs a bit more patching
		// - need to create heap in p9
		// - need to create thread in p9
		// for now just load high it's good enough, not like the game will be able to use sceNet properly after psplink wifi anyway
		SceKernelLMOption load_high = {
			.size = sizeof(SceKernelLMOption),
			.mpidtext = 2,
			.mpiddata = 2,
			.flags = 0,
			.position = PSP_SMEM_High,
			.access = 0,
			.creserved = {0, 0}
		};
		uint32_t k1 = pspSdkSetK1(0);
		int load_status = load_module_orig(path, flags, &load_high);
		pspSdkSetK1(k1);
		return load_status;
	}
	if (strstr(path, "modnet.prx") == NULL &&
		strstr(path, "netshell.prx") == NULL &&
		strstr(path, "netgdb.prx") == NULL &&
		strstr(path, "psplink_user.prx") == NULL &&
		strstr(path, "pspnet_apctl.prx") == NULL &&
		strstr(path, "pspnet_inet.prx") == NULL
	){
		return load_module_orig(path, flags, option);
	}

	SceKernelLMOption load_out = {
		.size = sizeof(SceKernelLMOption),
		.mpidtext = partition_to_use(),
		.mpiddata = partition_to_use(),
		.flags = 0,
		.position = PSP_SMEM_High,
		.access = 0,
		.creserved = {0, 0}
	};

	uint32_t k1 = pspSdkSetK1(0);
	int load_status = load_module_orig(path, flags, &load_out);
	pspSdkSetK1(k1);
	return load_status;
}

static void hook_module_loading(){
	HIJACK_FUNCTION(GET_JUMP_TARGET(*(uint32_t*)sceKernelLoadModule), load_module, load_module_orig);
}

#if 1
static int (*create_thread_orig)(const char *name, void *entry, int priority, int stack_size, int attr, void *option) = NULL;
static int create_thread(const char *name, void *entry, int priority, int stack_size, int attr, void *option){
	if (strcmp(name, "netshell recv thread") == 0 ||
		strcmp(name, "netshell send thread") == 0 ||
		strcmp(name, "netshell main") == 0
	){
		static SceKernelThreadOptParam opt = {.size = sizeof(SceKernelThreadOptParam), .stackMpid = 0};
		opt.stackMpid = partition_to_use();
		uint32_t k1 = pspSdkSetK1(0);
		int create_status = create_thread_orig(name, entry, priority, stack_size, PSP_THREAD_ATTR_USER, &opt);
		pspSdkSetK1(k1);
		return create_status;
	}
	if (strcmp(name, "PspLinkParse") == 0){
		return create_thread_orig(name, entry, priority, 256 * 1024, attr, option);

	}

	return create_thread_orig(name, entry, priority, stack_size, attr, option);
}

static void hook_thread_creation(){
	HIJACK_FUNCTION(GET_JUMP_TARGET(*(uint32_t*)sceKernelCreateThread), create_thread, create_thread_orig);
}
#else
#define hook_thread_creation(...)
#endif

static int (*create_heap_orig)(int part, int size, int unk, const char *name) = NULL;
static int create_heap(int part, int size, uint32_t unk, const char *name){
	// heap creation fails even when p2 has free space in some games.....
	if (strcmp(name, "SceNet") == 0){
		part = partition_to_use();
		unk = unk | 2; // seems to be high align flag
		int old_size = size;
		size = 3 * 1024 * 1024;
		LOG("%s: redirecting networking heap to partition %d and enlarging it to %d from %d\n", __func__, part, size, old_size);
	}
	int ret = create_heap_orig(part, size, unk, name);
	LOG("%s: part %d size %d unk 0x%lx name %s, 0x%x\n", __func__, part, size, unk, name, ret);

	return ret;
}

static void hook_heap_creation(){
	HIJACK_FUNCTION(GET_JUMP_TARGET(*(uint32_t *)sceKernelCreateHeap), create_heap, create_heap_orig);
}

int module_start(SceSize args, void *argp){
	LOG_INIT();
	LOG("%s: module started\n", __func__);
	hook_module_loading();
	hook_thread_creation();
	hook_heap_creation();
	memlayout_hack();
	log_memory_info();
	return 0;
}

int module_stop(SceSize args, void *argp){
	LOG("%s: attempting to stop this module, but unload is not really implemented...\n", __func__);
	return 0;
}

uint32_t get_kernel_delay_thread_function(){
	int value_in_kernel_stack = 0;
	return GET_JUMP_TARGET(*(uint32_t*)sceKernelDelayThread);
}
