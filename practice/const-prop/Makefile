
CC = gcc

GCCPLUGINS_DIR:= $(shell $(CC) -print-file-name=plugin)
INCLUDE= -I$(GCCPLUGINS_DIR)/include

FLAGS= -fPIC -O0

plugin.so: plugin.c
	$(CC) $(INCLUDE) $(FLAGS) -shared $^ -o $@

test: plugin.so test.c
	$(CC) -fplugin=./plugin.so test.c -fdump-tree-all
	cat *const*

clean:
	\rm -f plugin.so test *.c.* *~ *.o a.out


