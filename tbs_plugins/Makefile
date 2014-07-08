ifdef CROSS_COMPILE
	ARCH := $(CROSS_COMPILE)cross
else
	ARCH := $(shell uname -m)
endif

GLOBAL := /global
COMMON := /common
ACCAPP := /application
KATCPDIR := $(ACCAPP)/src/katcp_devel

OBJDIR := build
ARCHDIR := $(OBJDIR)/$(ARCH)
EXAMPLES := $(addprefix $(ARCHDIR)/, $(addsuffix .so, example alarm))
SWARM_ONLY := $(addprefix $(ARCHDIR)/, $(addsuffix .so, dsm sma_walsh sma_astro))
OBJS := $(EXAMPLES) $(SWARM_ONLY)

$(ARCHDIR)/sma_astro.so: LDLIBS = -lm
$(ARCHDIR)/dsm.so:       LDLIBS = -lrt -lpthread -ldsm -L$(COMMON)/lib

$(ARCHDIR)/sma_walsh.o:  IDIRS = -I$(GLOBAL)/dsm
$(ARCHDIR)/sma_astro.o:  IDIRS = -I$(GLOBAL)/dsm
$(ARCHDIR)/dsm.o:        IDIRS = -I$(GLOBAL)/dsm

examples: $(EXAMPLES)
swarm: $(SWARM_ONLY)
all: $(OBJS)

$(OBJS): | $(ARCHDIR)

$(ARCHDIR):
	mkdir -p $(ARCHDIR)

$(ARCHDIR)/%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -fPIC -Wall -c $< -o $@ -g \
		-I$(KATCPDIR)/katcp -I$(KATCPDIR)/tcpborphserver3 $(IDIRS) -DKATCP_USE_FLOATS \
		-D KATCP_PLUGIN_VERSION='"$(shell git describe --always --dirty)-$(shell git hash-object $< | head -c7)"'

$(ARCHDIR)/%.so: $(ARCHDIR)/%.o
	$(CROSS_COMPILE)ld -o $@ $^ -shared $(LDLIBS)

sma_astro.so: $(ARCHDIR)/sma_astro.o $(ARCHDIR)/lst.o
	$(CROSS_COMPILE)gcc -o $@ $^ -shared $(LDLIBS)

clean:
	rm -rf $(OBJDIR)
