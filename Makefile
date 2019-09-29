CFLAGS += -O2 -fpic -Wall `pkg-config gkrellm --cflags`

all: nvidia.so

nvidia.o: nvidia.c
	$(CC) $(CFLAGS) -c nvidia.c

nvidia.so: nvidia.o
	$(CC) -shared -lX11 -lXNVCtrl -onvidia.so nvidia.o

install:
	install -m755 nvidia.so ~/.gkrellm2/plugins/

clean:
	rm -rf *.o *.so

# start gkrellm in plugin-test mode
# (of course gkrellm has to be in PATH)
test: nvidia.so
	`which gkrellm` -p nvidia.so

.PHONY: install clean
