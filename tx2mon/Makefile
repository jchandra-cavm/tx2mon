CC	:= gcc
CFLAGS	:= -Werror -Wall -g -O2
LDFLAGS	:= -ltermcap

SRCS = tx2mon.c
OBJS = $(SRCS:.c=.o)
EXE = $(SRCS:.c=)

all: $(EXE) 

$(EXE): $(OBJS)

clean:
	rm -rf $(EXE) $(OBJS)

%:%.o 
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

%.o:%.c
	$(CC) $(CFLAGS) -c $<
