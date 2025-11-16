#
# Copyright (c) 2025 Hirokuni Yano (@hyano)
#
# Based on https://github.com/yunkya2/x68kz-zusb
# Copyright (c) 2025 Yuichi Nakamura (@yunkya2)
#
# The MIT License (MIT)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

SUBDIRS = dyptether

GIT_REPO_VERSION=$(shell git describe --tags --always)

all:
	-for d in $(SUBDIRS); do $(MAKE) -C $$d all; done

install: all
	rm -rf build
	mkdir build && (cd build && mkdir doc bin sys )
	cp README.md build/README.md
	-for d in $(SUBDIRS); do $(MAKE) -C $$d install; done
	./md2txtconv.py -r build/*.md build/doc/*.md GIT_REPO_VERSION=$(GIT_REPO_VERSION)

release: install
	(cd build && zip -r ../BlueSCSI-x68k-$(GIT_REPO_VERSION).zip README.txt bin sys doc)

clean:
	-rm -rf build
	-for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

.PHONY: all clean install
