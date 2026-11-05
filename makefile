PHONY: clean run compile test install all

PREFIX=/usr

all: clean compile test run

compile:
	meson compile -C builddir

clean:
	rm -rf builddir
	meson setup builddir --prefix=PREFIX

run:
	./builddir/winter

test: compile run

install:
    sudo meson install -C builddir
