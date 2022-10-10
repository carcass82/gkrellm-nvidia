CFLAGS += -O2 -fpic -Wall `pkg-config gkrellm --cflags`
LDFLAGS += -shared
LDLIBS  += -lX11 -lnvidia-ml

all: nvidia.so

nvidia.o: nvidia.c
	$(CC) $(CFLAGS) -c nvidia.c

nvidia.so: nvidia.o
	$(CC) $(LDFLAGS) $(LDLIBS) -o nvidia.so nvidia.o

install:
	install -m755 nvidia.so ~/.gkrellm2/plugins/

clean:
	rm -rf *.o *.so

# start gkrellm in plugin-test mode
# (make sure gkrellm executable is in PATH)
test: nvidia.so
	`which gkrellm` -p nvidia.so

.PHONY: install clean
