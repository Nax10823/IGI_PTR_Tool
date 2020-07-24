# Copyright (c) 2006
# Ningning Hu and the Carnegie Mellon University.
# All rights reserved. 
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author(s) may not be used to endorse or promote
#    products derived from this software without specific prior
#    written permission.  
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

CC 	= gcc 
INCS 	= -I. 
CFLAGS 	= -g -Wall $(DEFS) $(INCS)

# for linux
DEFS 	= -DLINUX -DRETSIGTYPE=void -DHAVE_SIGACTION=1
LIBS 	= -lpthread
# for SUN
#DEFS 	= -DSUN -DRETSIGTYPE=void -DHAVE_SIGACTION=1
#LIBS 	= -lsocket -lnsl -lthread -lpthread
# for FreeBSD
#DEFS 	= -DFreeBSD -DRETSIGTYPE=void -DHAVE_SIGACTION=1
#LIBS 	= -pthread

CLIOBJS = setsignal.o ptr-client.o
SRVOBJS = setsignal.o ptr-server.o

.c.o:
	@rm -f $@
	$(CC) $(CFLAGS) -c $*.c

all: ptr-server ptr-client

ptr-client: $(CLIOBJS)
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(CLIOBJS) $(LIBS)

ptr-server: $(SRVOBJS)
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(SRVOBJS) $(LIBS)

clean:
	rm -f *.o ptr-client ptr-server


