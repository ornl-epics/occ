SUBDIRS = driver lib libpy tools
SUBCLEAN = $(addsuffix .clean,$(SUBDIRS))

.PHONY: subdirs $(SUBDIRS) clean $(SUBCLEAN)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

tools: lib

clean: $(SUBCLEAN)
        
$(SUBCLEAN): %.clean:
	$(MAKE) -C $* -f Makefile clean
