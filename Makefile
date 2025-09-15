CFLAGS += -O2 -fpic -Wall -Wextra $(shell pkg-config gkrellm --cflags)

# stick to C17 to avoid callback parameters compile issues with C23
# https://gcc.gnu.org/gcc-15/porting_to.html#c23-fn-decls-without-parameters
CFLAGS += -std=c17

LDFLAGS += -shared
INSTALLFLAGS = -m755 -s

SOURCES = nvidia.c nvml-lib.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = nvidia.so

GKRELLM = $(shell which gkrellm)
INSTALL_DIR = /usr/lib/gkrellm2/plugins
LOCALINSTALL_DIR = $(HOME)/.gkrellm2/plugins

# maximum supported GPUs
MAX_GPUS := 4


all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $?

.c.o:
	$(CC) -c $(CFLAGS) -DGK_MAX_GPUS=$(MAX_GPUS) -o $@ $<

.PHONY: install install-local clean test

install: $(TARGET)
	install -d $(DESTDIR)$(INSTALL_DIR)
	install $(INSTALLFLAGS) $(TARGET) $(DESTDIR)$(INSTALL_DIR)

install-local: $(TARGET)
	install -d $(DESTDIR)$(LOCALINSTALL_DIR)
	install $(INSTALLFLAGS) $(TARGET) $(DESTDIR)$(LOCALINSTALL_DIR)

clean:
	rm -rf $(OBJECTS) $(TARGET)

# start gkrellm in plugin-test mode
# (needs gkrellm executable in PATH)
test: $(TARGET)
	$(GKRELLM) -p $<
