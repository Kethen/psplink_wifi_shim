TARGET = netshell
OBJS = main.o psplink.o modNet.o psplinkUser.o

PRX_EXPORTS=exports.exp
BUILD_PRX=1
USE_USER_LIBS=1

INCDIR = 
CFLAGS = -Os -G0 -Wall -g -I../modnet/
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LDFLAGS = -nostartfiles
LIBS=-nostdlib -lpspwlan -lpspuser -lpspnet_inet -lpspmodinfo

LIBDIR =

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
