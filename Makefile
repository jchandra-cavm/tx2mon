all:
	cd tx2mon && $(MAKE)
	cd modules && $(MAKE)

clean:
	$(MAKE) -C tx2mon clean
	$(MAKE) -C modules clean
