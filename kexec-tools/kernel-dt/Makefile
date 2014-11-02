#
# kernel-dt (cut and create dtb file)
#

KERNEL-DT_SRCS:= kernel-dt/kernel-dt.c

KERNEL-DT_OBJS = $(call objify, $(KERNEL-DT_SRCS))
KERNEL-DT_DEPS = $(call depify, $(KERNEL-DT_OBJS))

KERNEL-DT = $(SBINDIR)/kernel-dt
KERNEL-DT_MANPAGE = $(MANDIR)/man8/kernel-dt.8

dist += kernel-dt/Makefile $(KERNEL-DT_SRCS) kernel-dt/kernel-dt.8
clean += $(KERNEL-DT_OBJS) $(KERNEL-DT_DEPS) $(KERNEL-DT) $(KERNEL-DT_MANPAGE)

-include $(KERNEL-DT_DEPS)

$(KERNEL-DT): CC=$(TARGET_CC)
$(KERNEL-DT): $(KERNEL-DT_OBJS)
	@$(MKDIR) -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(KERNEL-DT_OBJS)

$(KERNEL-DT_MANPAGE): kernel-dt/kernel-dt.8
	$(MKDIR) -p     $(MANDIR)/man8
	cp kernel-dt/kernel-dt.8 $(KERNEL-DT_MANPAGE)
echo::
	@echo "KERNEL-DT_SRCS $(KERNEL-DT_SRCS)"
	@echo "KERNEL-DT_DEPS $(KERNEL-DT_DEPS)"
	@echo "KERNEL-DT_OBJS $(KERNEL-DT_OBJS)"

