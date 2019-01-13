
embasic-test: $(patsubst %.c,%.o,$(wildcard *.c))
	gcc $^ -o $@ -lm

%.o: %.c
	gcc -c -MD $<

include $(wildcard *.d)

clean:
	rm -f *.o *.d
	rm -f embasic-test test.lst test.pcode

test:
	./embasic-test -o test.pcode -l test.lst -v test.bas
	./embasic-test -r test.pcode
