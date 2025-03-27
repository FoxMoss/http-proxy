OBJS=build/httpproxy.o build/mongoose.o

all: $(OBJS)
	gcc -o build/httpproxy $(OBJS) -lcurl

build/%.o: %.c
	gcc -o $@ $< -c -g
