fault_test: fault_test.c
	gcc -o fault_test fault_test.c

.PHONY: clean

clean:
	rm -f fault_test *.o
