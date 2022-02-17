ifdef CROSS_COMPILE
	ARCH := $(CROSS_COMPILE)cross
else
	ARCH := $(shell uname -m)
	GLOBAL := /global
	COMMON := /common
	ACCAPP := /application
endif

KATCPDIR := $(ACCAPP)/src/katcp_devel

OBJDIR := build
ARCHDIR := $(OBJDIR)/$(ARCH)
EXAMPLES := $(addprefix $(ARCHDIR)/, $(addsuffix .so, example alarm))
SWARM_ONLY := $(addprefix $(ARCHDIR)/, $(addsuffix .so, dsm sma_adc sma_walsh sma_astro sma_dsm))
ADC_ONLY := $(addprefix $(ARCHDIR)/, $(addsuffix .so, sma_adc))
OBJS := $(EXAMPLES) $(SWARM_ONLY)

$(ARCHDIR)/sma_adc.so:   LDLIBS = -lm
$(ARCHDIR)/sma_dsm.so:   LDLIBS = -lm
$(ARCHDIR)/sma_astro.so: LDLIBS = -lm -lrt -lpthread -ldsm -L$(COMMON)/lib
$(ARCHDIR)/dsm.so:       LDLIBS = -lrt -lpthread -ldsm -L$(COMMON)/lib

examples: $(EXAMPLES)
swarm: $(SWARM_ONLY)
adc: $(ADC_ONLY)
all: $(OBJS)

$(OBJS): | $(ARCHDIR)

$(ARCHDIR):
	mkdir -p $(ARCHDIR)

$(ARCHDIR)/%.o: %.c
	$(CROSS_COMPILE)gcc -o $@ $(CFLAGS) -fPIC -Wall -c $< \
		-I$(ACCAPP)/include/slalib \
		-I$(KATCPDIR)/katcp -I$(KATCPDIR)/tcpborphserver3 -I$(GLOBAL)/dsm -I$(GLOBAL)/include -DKATCP_USE_FLOATS \
		-D KATCP_PLUGIN_VERSION='"$(shell git describe --always --dirty)-$(shell git hash-object $< | head -c7)"'

.INTERMEDIATE: $(ARCHDIR)/lst.o
$(ARCHDIR)/sma_astro.so: $(ARCHDIR)/lst.o
$(ARCHDIR)/%.so: $(ARCHDIR)/%.o
	$(CROSS_COMPILE)gcc -o $@ $^ -shared $(LDLIBS)

clean:
	rm -rf $(OBJDIR)

install: $(OBJS)
	install -d $(ACCAPP)/lib/tbs_plugins/
	install -D $(ARCHDIR)/*.so $(ACCAPP)/lib/tbs_plugins/
