CC := gcc

all:
	@./waf build

clean:
	@./waf clean

distclean:
	@./waf distclean
	@rm tags

ctags:
	@ctags `find -name \*.[ch]`
