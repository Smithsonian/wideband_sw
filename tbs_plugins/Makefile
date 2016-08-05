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
SWARM_ONLY := $(addprefix $(ARCHDIR)/, $(addsuffix .so, dsm sma_adc sma_walsh sma_astro sma_dsm_wait))
OBJS := $(EXAMPLES) $(SWARM_ONLY)

$(ARCHDIR)/sma_adc.so:   LDLIBS = -lm
$(ARCHDIR)/sma_dsm_wait.so: LDLIBS = -lm
$(ARCHDIR)/sma_astro.so: LDLIBS = -lm -lsla -L/usr/local/lib/slalib
$(ARCHDIR)/dsm.so:       LDLIBS = -lrt -lpthread -ldsm -L$(COMMON)/lib

examples: $(EXAMPLES)
swarm: $(SWARM_ONLY)
all: $(OBJS)

$(OBJS): | $(ARCHDIR)

$(ARCHDIR):
	mkdir -p $(ARCHDIR)

$(ARCHDIR)/%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -fPIC -Wall -c $< -o $@ -g \
		-I/usr/local/include/slalib \
		-I$(KATCPDIR)/katcp -I$(KATCPDIR)/tcpborphserver3 -I$(GLOBAL)/dsm -I$(GLOBAL)/include -DKATCP_USE_FLOATS \
		-D KATCP_PLUGIN_VERSION='"$(shell git describe --always --dirty)-$(shell git hash-object $< | head -c7)"'

.INTERMEDIATE: $(ARCHDIR)/lst.o
$(ARCHDIR)/sma_astro.so: $(ARCHDIR)/lst.o
$(ARCHDIR)/%.so: $(ARCHDIR)/%.o
	$(CROSS_COMPILE)gcc -o $@ $^ -shared $(LDLIBS)

clean:
	rm -rf $(OBJDIR)
