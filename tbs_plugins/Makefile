-include $(TARGET).make

$(TARGET)/example.so: LDLIBS = 
$(TARGET)/alarm.so: LDLIBS = -lrt
$(TARGET)/sma_walsh.so: LDLIBS = -lrt
$(TARGET)/sma_astro.so: LDLIBS = -lrt -lm 
$(TARGET)/dsm.so: LDLIBS = -lrt -ldsm -L$(COMMON)/lib

$(TARGET)/sma_walsh.o: IDIRS = -I$(GLOBAL)/dsm
$(TARGET)/sma_astro.o: IDIRS = -I$(GLOBAL)/dsm
$(TARGET)/dsm.o: IDIRS = -I$(GLOBAL)/dsm

build: 
	$(MAKE) TARGET=x86_64 all
	$(MAKE) TARGET=powerpc all

clean: 
	$(MAKE) TARGET=x86_64 clean-target
	$(MAKE) TARGET=powerpc clean-target

install: 
	$(MAKE) TARGET=x86_64 install-target
	$(MAKE) TARGET=powerpc install-target

all: $(TARGET) $(TARGET)/example.so $(TARGET)/alarm.so $(TARGET)/sma_walsh.so $(TARGET)/sma_astro.so $(TARGET)/dsm.so

$(TARGET):
	mkdir -p $(TARGET)

$(TARGET)/%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -fPIC -Wall -c $< -o $@ -g \
		-I$(KATCP)/katcp -I$(KATCP)/tcpborphserver3 $(IDIRS) -DKATCP_USE_FLOATS \
		-D KATCP_PLUGIN_VERSION='"$(shell git describe --always --dirty)-$(shell git hash-object $< | head -c7)"'

$(TARGET)/%.so: $(TARGET)/%.o 
	$(CROSS_COMPILE)gcc -o $@ $^ -shared $(LDLIBS)

$(TARGET)/sma_astro.so: $(TARGET)/sma_astro.o $(TARGET)/lst.o
	$(CROSS_COMPILE)gcc -o $@ $^ -shared $(LDLIBS)

clean-target:
	rm -f $(TARGET)/*.o
	rm -f $(TARGET)/*.so
	rmdir --ignore-fail-on-non-empty -p $(TARGET)

install-target:
	mkdir -p $(INSTALL_DIR)/tbs_plugins
	install -m 644 $(TARGET)/*.so $(INSTALL_DIR)/tbs_plugins
