# Makefile, ECE252  
# Yiqing Huang, 2018/11/02

CC = gcc 
CFLAGS = -Wall -std=gnu11 -g   
LD = gcc
LDFLAGS = -std=c99 -g 
LDLIBS = -lcurl -lz -lpthread  
LIB_UTIL = zutil.o crc.o

SRCS   = paster2.c crc.c zutil.c
OBJS4  = paster2.o zutil.o crc.o 
TARGETS = paster2

all: ${TARGETS}
paster2: $(OBJS4) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 


%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)


.PHONY: clean
clean:
	rm -f *~ *.d *.o $(TARGETS) *.png
