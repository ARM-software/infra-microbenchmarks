
# SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

CC = gcc
SRC = main.c bandwidth.c memlatency.c alloc.c args.c
CFLAGS = -O2 -Wall
LDFLAGS = -pthread
EXE = loaded-latency

OBJS = $(SRC:%.c=%.o)
DEPS = $(OBJS:%.o=.%.d)

.PHONY: all clean Makefile

all: $(EXE)

$(DEPS): .%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -c $<

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

include $(DEPS)

clean:
	rm -f $(EXE) $(OBJS) $(DEPS)
