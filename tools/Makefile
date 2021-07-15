SUBDIRS = proxy flash loopback OccDiag rawio
SUBCLEAN = $(addsuffix .clean,$(SUBDIRS))

.PHONY: subdirs $(SUBDIRS) clean $(SUBCLEAN)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(SUBCLEAN)
        
$(SUBCLEAN): %.clean:
	$(MAKE) -C $* -f Makefile clean
