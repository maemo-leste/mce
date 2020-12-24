# Makefile for MCE
# Copyright © 2004-2001 Nokia Corporation.
# Written by David Weinehall

VERSION := 1.8.128

INSTALL := install -o root -g root --mode=755
INSTALL_DIR := install -d
INSTALL_DATA := install -o root -g root --mode=644

VARDIR := $(DESTDIR)/var/lib/mce
RUNDIR := $(DESTDIR)/var/run/mce
CONFDIR := /etc/mce
OVRCONFDIR := mce.ini.d
CONFINSTDIR := $(DESTDIR)$(CONFDIR)
SBINDIR := $(DESTDIR)/sbin
MODULEDIR := $(DESTDIR)/usr/lib/mce/modules
DBUSDIR := $(DESTDIR)/etc/dbus-1/system.d
LOCALEDIR := $(DESTDIR)/usr/share/locale
GCONFSCHEMADIR := $(DESTDIR)/etc/gconf/schemas

TOPDIR := $(shell /bin/pwd)
MODULE_DIR := $(TOPDIR)/modules

TARGETS := \
	mce
MODULES := \
	$(MODULE_DIR)/libfilter-brightness-als.so \
	$(MODULE_DIR)/libfilter-brightness-als-iio.so \
	$(MODULE_DIR)/libfilter-brightness-simple.so \
	$(MODULE_DIR)/libiio-proximity.so \
	$(MODULE_DIR)/libkeypad.so \
	$(MODULE_DIR)/libinactivity.so \
	$(MODULE_DIR)/libinactivity-dev.so \
	$(MODULE_DIR)/libinactivity-inhibit.so \
	$(MODULE_DIR)/libcamera.so \
	$(MODULE_DIR)/libalarm.so \
	$(MODULE_DIR)/libbattery-upower.so \
	$(MODULE_DIR)/libdisplay.so \
	$(MODULE_DIR)/libdisplay-dev.so \
	$(MODULE_DIR)/libled-lysti.so \
	$(MODULE_DIR)/libled-sw.so \
	$(MODULE_DIR)/libled-dbus.so \
	$(MODULE_DIR)/libevdevvibrator.so \
	$(MODULE_DIR)/libiio-accelerometer.so \
	$(MODULE_DIR)/libcallstate.so \
	$(MODULE_DIR)/libaudiorouting.so \
	$(MODULE_DIR)/libhomekey.so \
	$(MODULE_DIR)/libx11-ctrl.so \
	$(MODULE_DIR)/libiio-als.so \
	$(MODULE_DIR)/libbutton-backlight.so
MODEFILE := mode
CONFFILE := mce.ini
USRCONFFILE := 99-user.ini
DBUSCONF := mce.conf
GCONFSCHEMAS := devicelock.schemas security.schemas

WARNINGS := -Wextra -Wall -Wpointer-arith -Wundef -Wcast-align -Wshadow
WARNINGS += -Wbad-function-cast -Wwrite-strings -Wsign-compare
WARNINGS += -Wmissing-noreturn -Wnested-externs
WARNINGS += -Wchar-subscripts -Wmissing-prototypes -Wformat-security
WARNINGS += -Wformat=2 -Wformat-nonliteral -Winit-self
WARNINGS += -Wswitch-default -Wstrict-prototypes
WARNINGS += -Wold-style-definition -Wmissing-declarations
WARNINGS += -Wmissing-include-dirs -Wstrict-aliasing=2
WARNINGS += -Wunsafe-loop-optimizations -Winvalid-pch
WARNINGS += -Waddress -Wvolatile-register-var -Wstrict-overflow=5
#WARNINGS += -Wmissing-format-attribute
#WARNINGS += -Wswitch-enum -Wunreachable-code
WARNINGS += -Wstack-protector
#WARNINGS += -Werror

COMMON_CFLAGS := $(WARNINGS)
COMMON_CFLAGS += -D_GNU_SOURCE
COMMON_CFLAGS += -DG_DISABLE_DEPRECATED
COMMON_CFLAGS += -DOSSOLOG_COMPILE
COMMON_CFLAGS += -DMCE_VAR_DIR=$(VARDIR) -DMCE_RUN_DIR=$(RUNDIR)
COMMON_CFLAGS += -DPRG_VERSION=$(VERSION)
#COMMON_CFLAGS += -funit-at-a-time -fwhole-program -combine
#COMMON_CFLAGS += -fstack-protector

MCE_CFLAGS := $(COMMON_CFLAGS)
MCE_CFLAGS += -DMCE_CONF_FILE=$(CONFFILE) -DMCE_CONF_DIR=$(CONFDIR) -DMCE_CONF_OVERRIDE_DIR=$(OVRCONFDIR)
MCE_CFLAGS += $$(pkg-config glib-2.0 gio-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0 osso-systemui-dbus conic x11 xi --cflags)
MCE_LDFLAGS := $$(pkg-config glib-2.0 gio-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0 dsme osso-systemui-dbus conic libdevlock1 x11 xi xext --libs)
LIBS := devlock.c tklock.c modetransition.c powerkey.c connectivity.c mce-dbus.c mce-dsme.c mce-gconf.c event-input.c event-switches.c mce-log.c mce-conf.c datapipe.c mce-modules.c mce-io.c mce-lib.c event-input-utils.c
HEADERS := devlock.h tklock.h modetransition.h powerkey.h connectivity.h mce.h mce-dbus.h mce-dsme.h mce-gconf.h event-input.h event-switches.h mce-log.h mce-conf.h datapipe.h mce-modules.h mce-io.h mce-lib.h event-input-utils.h

MODULE_CFLAGS := $(COMMON_CFLAGS)
MODULE_CFLAGS += -fPIC -shared
MODULE_CFLAGS += -I.
MODULE_CFLAGS += $$(pkg-config glib-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0 upower-glib --cflags)
MODULE_LDFLAGS := $$(pkg-config glib-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0 upower-glib libcal x11 xext --libs)
MODULE_LIBS := datapipe.c mce-log.c mce-dbus.c mce-conf.c mce-gconf.c median_filter.c mce-lib.c event-input-utils.c 
MODULE_HEADERS := datapipe.h mce-log.h mce-dbus.h mce-conf.h mce-gconf.h mce.h median_filter.h mce-lib.h event-input-utils.h

BLOCKER_CFLAGS := $(COMMON_CFLAGS)
BLOCKER_CFLAGS += $$(pkg-config glib-2.0 dbus-1 dbus-glib-1 --cflags)
BLOCKER_LDFLAGS := $$(pkg-config glib-2.0 dbus-1 dbus-glib-1 --libs)

ifeq (,$(wildcard /etc/devuan_version))
	MCE_CFLAGS += -DENABLE_SYSTEMD_SUPPORT
	MCE_LDFLAGS += $$(pkg-config libsystemd --libs)
endif

.PHONY: all
all: $(TARGETS) $(MODULES) devlock-blocker

$(TARGETS): %: %.c $(HEADERS) $(LIBS)
	@$(CC) $(CFLAGS) $(MCE_CFLAGS) -o $@ $< $(LIBS) $(LDFLAGS) $(MCE_LDFLAGS)

$(MODULES): $(MODULE_DIR)/lib%.so: $(MODULE_DIR)/%.c $(MODULE_HEADERS) $(MODULE_LIBS)
	@$(CC) $(CFLAGS) $(MODULE_CFLAGS) -o $@ $< $(MODULE_LIBS) $(LDFLAGS) $(MODULE_LDFLAGS)

devlock-blocker: devlock-blocker.c mce-log.c mce-log.h
	@$(CC) $(CFLAGS) $(BLOCKER_CFLAGS) -o $@ $< $(LDFLAGS) mce-log.c $(BLOCKER_LDFLAGS)

.PHONY: tags
tags:
	@find . $(MODULE_DIR) -maxdepth 1 -type f -name '*.[ch]' | xargs ctags -a --extra=+f

.PHONY: install
install: all
	$(INSTALL_DIR) $(SBINDIR) $(DBUSDIR) $(VARDIR) $(MODULEDIR)	&&\
	$(INSTALL_DIR) $(RUNDIR) $(CONFINSTDIR)	$(GCONFSCHEMADIR) 	&&\
	$(INSTALL_DIR) $(CONFINSTDIR)/$(OVRCONFDIR)			&&\
	$(INSTALL) $(TARGETS) devlock-blocker $(SBINDIR)		&&\
	$(INSTALL) $(MODULES) $(MODULEDIR)				&&\
	$(INSTALL_DATA) $(MODEFILE) $(VARDIR)				&&\
	$(INSTALL_DATA) $(CONFFILE) $(CONFINSTDIR)			&&\
	$(INSTALL_DATA) $(USRCONFFILE) $(CONFINSTDIR)/$(OVRCONFDIR)	&&\
	$(INSTALL_DATA) $(GCONFSCHEMAS) $(GCONFSCHEMADIR)		&&\
	$(INSTALL_DATA) $(DBUSCONF) $(DBUSDIR)

.PHONY: fixme
fixme:
	@find . -type f -name "*.[ch]" | xargs grep -E "FIXME|XXX|TODO"

.PHONY: clean
clean:
	@rm -f $(TARGETS) $(MODULES) devlock-blocker

.PHONY: distclean
distclean: clean
	@rm -f tags
