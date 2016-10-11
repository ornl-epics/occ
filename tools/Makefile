SUBDIRS = dump flash loopback PacketAnalyzer rawio

.PHONY: subdirs $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@
