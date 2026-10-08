PHONY: clean run compile test all

all:

compile:
	meson compile -C builddir

clean:
	rm -rf builddir
	meson setup builddir

run:
	./builddir/winter

test: compile run
