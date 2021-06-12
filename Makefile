all:
	$(MAKE) -C lib
	$(MAKE) -C example

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C example clean

.PHONY: all clean
