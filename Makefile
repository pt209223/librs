
all: ctags
	make -C rs all
	make -C bot all
	@echo "Everything done!"

work: all
	cp bot/Bot work/Bot
	@echo "Binary copied!"

ctags:
	@ctags ./*/*.{cc,hh}

clean:
	make -C rs clean
	make -C bot clean
	@rm -f tags
	@echo "Everything cleaned!"

