core := lib
remaining := examples benchmark tests
subdirs := $(core) $(remaining)
source_types := .h .c .cc .cpp 
all_sources := $(foreach dir,$(subdirs),$(foreach stype,$(source_types),$(wildcard $(dir)/*$(stype)) $(wildcard $(dir)/**/*$(stype))))

all: $(subdirs)

$(subdirs):
	$(MAKE) -C $@

$(remaining): $(core)

clean:
	@for dir in $(subdirs); do \
		$(MAKE) -C $$dir clean; \
	done

format:
	@cmake/script/clang-format-changed.py --in-place

format-check:
	@cmake/script/clang-format-changed.py --check-only 
	@echo "Format check passed"

format-all:
	@echo "Running clang-format on " $(all_sources)
	clang-format $(all_sources)

tags: $(all_sources)
	ctags -R .

.PHONY: all clean $(subdirs) format format-all format-check
