TARGET = netshell
OBJS = main.o psplink.o modNet.o psplinkUser.o psplink_wifi_shim.o

PRX_EXPORTS=exports.exp
BUILD_PRX=1
USE_USER_LIBS=1

INCDIR = 
CFLAGS = -Os -G0 -fno-pic -Wall -g
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LDFLAGS = -nostartfiles
LIBS=-nostdlib -lpspwlan -lpspuser -lpspnet_inet -lpspmodinfo

LIBDIR =

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
