# Shut up GNU make
.SILENT:

OBJECTIVE_DIRECTORIES = none
OBJECTIVE_LIBS = none
OBJECTIVE_BINS = none
SUBDIRS = none
CFLAGS += -DHAVE_CONFIG_H

default: all
all: build

install:
	$(MAKE) install-prehook
	$(INSTALL) -d -m 755 $(DESTDIR)/$(LIBDIR)
	@if test "$(OBJECTIVE_DIRECTORIES)" != "none"; then \
		for i in $(OBJECTIVE_DIRECTORIES); do \
			printf "%10s     %-20s\n" MKDIR $$i; \
			$(INSTALL) -d -m 755 $(DESTDIR)/$$i; \
		done; \
	fi
	@if test "$(OBJECTIVE_LIBS)" != "none"; then \
		for i in $(OBJECTIVE_LIBS); do \
			printf "%10s     %-20s\n" INSTALL $$i; \
			$(INSTALL) $(INSTALL_OVERRIDE) $$i $(DESTDIR)/$(LIBDIR)/$$i; \
		done; \
	fi
	@if test "$(OBJECTIVE_BINS)" != "none"; then \
		for i in $(OBJECTIVE_BINS); do \
			printf "%10s     %-20s\n" INSTALL $$i; \
			$(INSTALL) $(INSTALL_OVERRIDE) $$i $(DESTDIR)/$(BINDIR)/$$i; \
		done; \
	fi;
	$(MAKE) install-posthook
	@echo "[all objectives installed]"

clean:
	$(MAKE) clean-prehook
	@if test "$(SUBDIRS)" != "none"; then \
		for i in $(SUBDIRS); do \
			echo "[cleaning subobjective: $$i]"; \
			(cd $$i; $(MAKE) clean; cd ..); \
		done; \
	fi
	$(MAKE) clean-posthook
	$(RM) *.o *.lo *.so *.a *.sl
	@echo "[all objectives cleaned]"

distclean: clean
	$(RM) mk/rules.mk

build:
	$(MAKE) build-prehook
	@if test "$(SUBDIRS)" != "none"; then \
		for i in $(SUBDIRS); do \
			echo "[building subobjective: $$i]"; \
			(cd $$i; $(MAKE); cd ..); \
			echo "[finished subobjective: $$i]"; \
		done; \
	fi
	@for i in $(OBJECTIVE_LIBS); do \
		echo "[building library objective: $$i]"; \
		$(MAKE) $$i; \
		echo "[finished library objective: $$i]"; \
	done
	@for i in $(OBJECTIVE_LIBS_NOINST); do \
		echo "[building library dependency: $$i]"; \
		$(MAKE) $$i; \
		echo "[finished library dependency: $$i]"; \
	done
	@for i in $(OBJECTIVE_BINS); do \
		echo "[building binary objective: $$i]"; \
		$(MAKE) $$i; \
		echo "[finished binary objective: $$i]"; \
	done
	$(MAKE) build-posthook
	@echo "[all objectives built]"

.c.o:
	printf "%10s     %-20s\n" CC $<
	$(CC) $(CFLAGS) -c $< -o $@

.cc.o:
	printf "%10s     %-20s\n" CXX $<;
	$(CXX) $(CXXFLAGS) -c $< -o $@

.cpp.o:
	printf "%10s     %-20s\n" CXX $<;
	$(CXX) $(CXXFLAGS) -c $< -o $@

.cxx.o:
	printf "%10s     %-20s\n" CXX $<;
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.so: $(OBJECTS)
	make $(OBJECTS)
	printf "%10s     %-20s\n" LINK $@
	$(CC) -fPIC -DPIC -shared -o $@ $(OBJECTS) $(LDFLAGS) $(LIBADD)

clean-prehook:
clean-posthook:
build-prehook:
build-posthook:
install-prehook:
install-posthook:

# compatibility with automake follows
am--refresh:
