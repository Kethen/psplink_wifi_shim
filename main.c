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
#include <stdarg.h>

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

// vsnprintf from aemu, likely by mrcoldbird, or imported from somewhere by mrcoldbird
static int itostr(char *buf, int in_data, int base, int upper, int sign)
{
	int res, len, i;
	unsigned int data;
	char *str;

	if(base==10 && sign && in_data<0){
		data = -in_data;
	}else{
		data = in_data;
	}

	str = buf;
	do{
		res = data%base;
		data = data/base;
		if(res<10){
			res += '0';
		}else{
			if(upper){
				res += 'A'-10;
			}else{
				res += 'a'-10;
			}
		}
		*str++ = res;
	}while(data);
	len = str-buf;

	/* reverse digital order */
	for(i=0; i<len/2; i++){
		res = buf[i];
		buf[i] = buf[len-1-i];
		buf[len-1-i] = res;
	}

	return len;
}

#define OUT_C(c) \
if(str<end){ \
	*str++ = (c); \
} else { \
	goto exit; \
}
static int _vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	static char digital_buf[32] = {0};
	char ch, *s, *str, *end, *sstr;
	int zero_pad, left_adj, add_sign, field_width, sign;
	int i, base, upper, len;

	if(!buf || !fmt ||!size){
		return 0;
	}

	str = buf;
	end = buf+size;

	while(*fmt){
		if(*fmt!='%'){
			OUT_C(*fmt++);
			continue;
		}

		/* skip '%' */
		sstr = (char *)fmt;
		fmt++;

		/* %% */
		if(*fmt=='%'){
			OUT_C(*fmt++);
			continue;
		}

		/* get flag */
		zero_pad = ' ';
		left_adj = 0;
		add_sign = 0;
		while((ch=*fmt)){

			if(*fmt=='0'){
				zero_pad = '0';
			}else if(*fmt=='-'){
				left_adj = 1;
			}else if(*fmt=='#'){
			}else if(*fmt==' '){
				if(add_sign!='+')
					add_sign = ' ';
			}else if(*fmt=='+'){
				add_sign = '+';
			}else{
				break;
			}
			fmt++;
		}

		/* get field width: m.n */
		field_width = 0;
		/* get m */
		while(*fmt && *fmt>'0' && *fmt<='9'){
			field_width = field_width*10+(*fmt-'0');
			fmt++;
		}
		if(*fmt && *fmt=='.'){
			fmt++;
			/* skip n */
			while(*fmt && *fmt>'0' && *fmt<='9'){
				fmt++;
			}
		}

		/* get format char */
		upper = 0;
		base = 0;
		sign = 0;
		len = 0;
		s = digital_buf;
		while((ch=*fmt)){
			fmt++;
			switch(ch){
				/* hexadecimal */
				case 'p':
				case 'X':
					upper = 1;
				case 'x':
					base = 16;
					break;

					/* decimal */
					case 'd':
					case 'i':
						sign = 1;
					case 'u':
						base = 10;
						break;

						/* octal */
						case 'o':
							base = 8;
							break;

							/* character */
							case 'c':
								digital_buf[0] = (unsigned char) va_arg(args, int);
								len = 1;
								break;

								/* string */
								case 's':
									s = va_arg(args, char *);
									if(!s) s = "<NUL>";
									len = strlen(s);
				break;

				/* float format, skip it */
				case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
					va_arg(args, double);
					s = NULL;
					break;

					/* length modifier */
					case 'l': case 'L': case 'h': case 'j': case 'z': case 't':
						/* skip it */
						continue;

						/* bad format */
						default:
							s = sstr;
							len = fmt-sstr;
							break;
			}
			break;
		}

		if(base){
			i = va_arg(args, int);
			if(base==10 && sign){
				if(i<0){
					add_sign = '-';
				}
			}else{
				add_sign = 0;
			}

			len = itostr(digital_buf, i, base, upper, sign);
		}else{
			zero_pad = ' ';
			add_sign = 0;
		}

		if(s){
			if(len>=field_width){
				field_width = len;
				if(add_sign)
					field_width++;
			}
			for(i=0; i<field_width; i++){
				if(left_adj){
					if(i<len){
						OUT_C(*s++);
					}else{
						OUT_C(' ');
					}
				}else{
					if(add_sign && (zero_pad=='0' || i==(field_width-len-1))){
						OUT_C(add_sign);
						add_sign = 0;
						continue;
					}
					if(i<(field_width-len)){
						OUT_C(zero_pad);
					}else{
						OUT_C(*s++);
					}
				}
			}
		}
	}

	OUT_C(0);

	exit:
	return str-buf;
}

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
		strcmp(name, "netshell main") == 0 ||
		strcmp(name, "SceNetNetintr") == 0 ||
		strcmp(name, "SceNetCallout") == 0
	){
		static SceKernelThreadOptParam opt = {.size = sizeof(SceKernelThreadOptParam), .stackMpid = 0};
		opt.stackMpid = partition_to_use();
		uint32_t k1 = pspSdkSetK1(0);
		int create_status = create_thread_orig(name, entry, priority, stack_size, PSP_THREAD_ATTR_USER, &opt);
		pspSdkSetK1(k1);
		return create_status;
	}
	if (strcmp(name, "PspLinkParse") == 0){
		return create_thread_orig(name, entry, priority, stack_size, attr, option);
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

static void *find_module_func_offset_by_pattern(const char *mod_name, const uint8_t *pattern, int pattern_len){
	SceUID module_ids[1024];
	int num_modules = 0;

	sceKernelGetModuleIdList(module_ids, sizeof(module_ids), &num_modules);
	for(int i = 0;i < num_modules;i++){
		SceKernelModuleInfo info = {0};
		info.size = sizeof(info);
		int query_status = sceKernelQueryModuleInfo(module_ids[i], &info);
		if (query_status != 0){
			continue;
		}

		if (strcmp(info.name, mod_name) != 0){
			continue;
		}

		for (uint32_t offset = 0;offset < info.text_size - pattern_len;offset++){
			uint8_t *check = (uint8_t *)(info.text_addr + offset);
			if (memcmp(check, pattern, pattern_len) == 0){
				return check;
			}
		}
	}

	return NULL;
}

static const char *to_psplink_input = NULL;
static int psplink_input_ret = 2;

int send_input_to_psplink_thread_func(unsigned int args, void *arg){
	// this is funky, last release of wifi psplink sends a mbx on stack memory, so this is to create stable-ish stack memory for that function
	while(1){
		if (to_psplink_input == NULL){
			sceKernelDelayThread(5000);
			continue;
		}

		static int (*psplink_parse)(const char *input) = NULL;
		static int (*fast_path)(const char *input) = NULL;
		static void (*print_prompt)() = NULL;
		if (psplink_parse == NULL){
			psplink_parse = (void *)sctrlHENFindFunction("PSPLINK", "psplink", 0x8B5F450B);
			LOG("%s: psplink_parse found at 0x%x\n", __func__, (unsigned int)psplink_parse);
		}
		if (print_prompt == NULL){
			print_prompt = (void *)sctrlHENFindFunction("PSPLINK", "psplink", 0xE3010EA1);
			LOG("%s: print_prompt found at 0x%x\n", __func__, (unsigned int)print_prompt);
		}
		if (fast_path == NULL){
			static const uint8_t pattern[] = {0xa0, 0xeb, 0xbd, 0x27, 0x4c, 0x14, 0xb0, 0xaf};
			fast_path = find_module_func_offset_by_pattern("PSPLINK", pattern, sizeof(pattern));
			LOG("%s: fast_path found at 0x%x\n", __func__, (unsigned int)fast_path);
		}
		if (psplink_parse == NULL && fast_path == NULL){
			// psplink is likely not loaded yet
			psplink_input_ret = 0;
			to_psplink_input = NULL;
			continue;
		}

		if (fast_path != NULL){
			// just need to block here, the other side does not handle message status
			// blocking here also fixes parse sychrnoization issues during parsing
			//LOG("%s: using fast path parsing\n", __func__);
			fast_path(to_psplink_input);
			//LOG("%s: fast path parsing finished, printing prompt\n", __func__);
			if (print_prompt != NULL) print_prompt();
			//LOG("%s: prompt print finished\n", __func__);
			psplink_input_ret = 0;
			to_psplink_input = NULL;
			continue;
		}

		// mbx is weird in there, we ideally get the fast path and run that directly, since wifi is the only expected shell anyway
		psplink_input_ret = psplink_parse(to_psplink_input);
		to_psplink_input = NULL;
	}
}

int send_input_to_psplink(const char *input){
	psplink_input_ret = 2;
	to_psplink_input = input;

	while(psplink_input_ret == 2){
		sceKernelDelayThread(5000);
	}

	return psplink_input_ret;
}

void create_psplink_send_thread(){
	int tid = sceKernelCreateThread("psplink_wifi_shim stack hack", send_input_to_psplink_thread_func, 90, 256 * 1024, 0, NULL);
	sceKernelStartThread(tid, 0, NULL);
}

int (*is_cpu_intr_enable_function)() = NULL;
uint32_t get_is_cpu_intr_enable_function(){
	if (is_cpu_intr_enable_function == NULL){
		is_cpu_intr_enable_function = (void *)sctrlHENFindFunction("sceKernelLibrary", "Kernel_Library", 0xB55249D2);
	}
	return (uint32_t)is_cpu_intr_enable_function;
}

static int (*buffer_consumer)(const char *, int size) = NULL;
static char *buffer_data = NULL;
static unsigned int buffer_max_size = 0;
static unsigned int *buffer_size = NULL;
static int *buffer_lock = NULL;
static int *buffer_cur_sock = NULL;
static int fill_buffer(const char *c, int s){
	if (is_cpu_intr_enable_function == NULL){
		get_is_cpu_intr_enable_function();
	}

	if (buffer_consumer == NULL){
		return s;
	}

	if (*buffer_cur_sock < 0){
		return s;
	}

	if (!is_cpu_intr_enable_function() && *buffer_lock){
		return s;
	}

	int total_wait_time = 0;
	while(*buffer_lock){
		sceKernelDelayThread(5000);
		total_wait_time += 5000;
		if (total_wait_time >= 1000000){
			return s;
		}
	}
	*buffer_lock = 1;
	if (*buffer_size + s <= buffer_max_size){
		memcpy(&buffer_data[*buffer_size], c, s);
		*buffer_size = *buffer_size + s;
	}
	*buffer_lock = 0;
	return s;
}

void register_buffer_consumer(void *consumer, char *data, unsigned int max_size, unsigned int *size, int *lock, int *cur_sock){
	buffer_consumer = consumer;
	buffer_data = data;
	buffer_max_size = max_size;
	buffer_size = size;
	buffer_lock = lock;
	buffer_cur_sock = cur_sock;

	void (*tty_set_wifi_handler)(void *handler) = (void *)sctrlHENFindFunction("PSPLINK", "psplink", 0x4DFA5010);
	//tty_set_wifi_handler(consumer);
	// calling a user function from kernel does not always work properly, prone to randdom crashes
	tty_set_wifi_handler(fill_buffer);
}

static int (*printf_orig)(const char *fmt, ...) = NULL;
static int printf_patched(const char *fmt, ...){
	static char buf[256] = {0};
	va_list args;
	va_start(args, fmt);
	int len = _vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	#if 1
	if (buffer_consumer != NULL){
		//buffer_consumer(buf, len);
		fill_buffer(buf, len);
	}
	#else
	LOG("%s", buf);
	#endif
	return len;
}

static void hook_printf(){
	// tty handler calling is also prone to crahses, let's do a bypass
	uint32_t proc = sctrlHENFindFunction("sceIOFileManager", "StdioForKernel", 0xCAB439DF);
	HIJACK_FUNCTION(proc, printf_patched, printf_orig);
}

int module_start(SceSize args, void *argp){
	LOG_INIT();
	LOG("%s: module started\n", __func__);
	hook_module_loading();
	hook_thread_creation();
	hook_heap_creation();
	memlayout_hack();
	log_memory_info();
	create_psplink_send_thread();
	hook_printf();
	return 0;
}

int module_stop(SceSize args, void *argp){
	LOG("%s: attempting to stop this module, but unload is not really implemented...\n", __func__);
	return 0;
}

uint32_t get_kernel_delay_thread_function(){
	return GET_JUMP_TARGET(*(uint32_t*)sceKernelDelayThread);
}


