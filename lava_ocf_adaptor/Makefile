#
# Copyright(c) 2012-2021 Intel Corporation
# Copyright(c) 2024 Huawei Technologies
# SPDX-License-Identifier: BSD-3-Clause
#

OCFDIR=../
SRCDIR=src/
INCDIR=include/

SRC=$(shell find ${SRCDIR} -name \*.c)
OBJS = $(patsubst %.c, %.o, $(SRC))

SRC_CPP=$(shell find ${SRCDIR} -name \*.cpp)
OBJS_CPP = $(patsubst %.cpp, %.o, $(SRC_CPP))

LIB=liblava_cache.so

CC = gcc
CPPFLAGS = -fPIC -g -Wall -I${INCDIR} -I${SRCDIR}/ocf/env/ -I${SRCDIR}/ocf/utils/
LDFLAGS = -lm -lz -pthread

all: sync
	$(MAKE) $(LIB)

$(LIB): $(OBJS) $(OBJS_CPP)
	$(CC) -shared -fpic -o $@ $^ $(LDFLAGS)

sync:
	@$(MAKE) -C ${OCFDIR} inc O=$(PWD)
	@$(MAKE) -C ${OCFDIR} src O=$(PWD)
	@$(MAKE) -C ${OCFDIR} env O=$(PWD) OCF_ENV=posix

install:
ifeq ($(strip $(DESTDIR)),)
	$(error No target directory specified for installation)
endif
	install -d $(DESTDIR)/usr/include
	install -Dp $(PWD)/include/ocf_adaptor* $(DESTDIR)/usr/include
	install -Dp $(PWD)/$(LIB) $(DESTDIR)/usr/lib64/$(LIB)

uninstall:
ifeq ($(strip $(DESTDIR)),)
	$(error No target directory specified for uninstallation)
endif
	rm -rf $(DESTDIR)/usr

clean:
	@rm -rf $(LIB) $(OBJS) $(OBJS_CPP)

distclean:
	@rm -rf $(LIB) $(OBJS) $(OBJS_CPP)
	@rm -rf src/ocf
	@rm -rf include/ocf

.PHONY: all clean install uninstall

