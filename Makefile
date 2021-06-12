all:
	$(MAKE) -C lib
	$(MAKE) -C example
	$(MAKE) -C benchmark
	$(MAKE) -C test

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C example clean
	$(MAKE) -C benchmark clean
	$(MAKE) -C test clean

.PHONY: all clean
