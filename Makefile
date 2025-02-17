OBJS=build/httpproxy.o build/mongoose.o

all: $(OBJS)
	gcc -o build/httpproxy $(OBJS) -lcurl -fsanitize=address

build/%.o: %.c
	gcc -o $@ $< -c -g
