CC = gcc
LD = gcc

CFLAGS = -ansi -pedantic-errors -Wall -Wextra -g  -pthread\
		 -iquote ../../ds/single_threaded_scheduler/ \
		  -iquote ../../ds/doubly_linked_list/ \
		  -iquote ../../ds/sorted_dl_list/ \
		  -iquote ../../ds/uid \
		  -iquote ../../system_programming/process_semaphore

LFLAGS = $(CFLAGS) -L/usr/lib/ 
VPATH   = ../../ds/single_threaded_scheduler/: \
	  	  ../../ds/doubly_linked_list/: \
	  	  ../../ds/sorted_dl_list/: \
	  	  ../../ds/uid: \
	  	  ../../system_programming/process_semaphore

OBJECTS := wd.o wd_app.o st_scheduler.o task.o uid.o \
		  dl_list.o sorted_dl_list.o process_semaphore.o

all: app wd_bg_p

app: $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(LFLAGS) -o $@

wd_bg_p: $(filter-out wd_app.o, $(OBJECTS)) wd_bg_p.o
	$(CC) $(filter-out wd_app.o, $(OBJECTS)) wd_bg_p.o $(CFLAGS) $(LDFLAGS) -o $@


clean:
	rm -rf *.o app wd_bg_p

obj_clean:
	rm -rf *.o

rebuild: clean all obj_clean

.PHONY : clean obj_clean
