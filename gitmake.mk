#!/usr/bin/make -f

VPATH=.:./.git/refs/heads/

gitmake-all: cd diaglvl
	touch gitmake-all
	git checkout patches
	rm -rf current
	mv patches/* .
	mkdir patches/current
	git add *.diff
	git add current/*
	git commit --allow-empty -m "patches for version `cat current/TRUNK_VERSION.txt`"
	git checkout gitmake

check: clean gitmake-origin
	echo "check OK"

clean:
	rm -f gitmake-origin
	rm -rf patches
	mkdir -p patches/current

%:
	git checkout ${@}-tmp
	for branch in $(^F); do git merge $${branch}; done 
	git checkout $@
	git merge ${@}-tmp
	test -e gitmake-build && make -j`cat gitmake-build` || true
	./make_diffs

gitmake-origin:
	git fetch git://git.openttd.org/openttd/trunk.git master:master
	git log master | grep -m 1 '(svn r[0-9]*)' | awk '{print $$2}' | sed -e 's/)//' > gitmake-origin
	cp gitmake-origin patches/current/TRUNK_VERSION.txt
	git diff --numstat master gitmake | grep -v .gitignore | grep -v gitmake | grep -v make_diffs && touch gitmake-origin || touch -t 197001010100 gitmake-origin

gitmake: gitmake-origin
	git checkout $@
	git merge master
	test -e gitmake-build && make -j`cat gitmake-build` || true

smallmap-zoom-in: gitmake 

cd: station-gui smallmap-stats warning-sign ext-rating

ext-rating: cargomap

station-gui: cargomap

smallmap-stats: smallmap-zoom-in cargomap

texteff: gitmake 

flowmapping-core: mcf

mcf: demands

demands: components

components: capacities

capacities: moving-average

diaglvl: tileiter

tileiter: gitmake 

selfaware-stationcargo: gitmake 

cargomap: flowmapping-core texteff multimap reservation selfaware-stationcargo

reservation: gitmake 

multimap: gitmake 

warning-sign: demands

moving-average: gitmake 

push: master gitmake patches cd ext-rating station-gui smallmap-stats flowmapping-core mcf demands components capacities smallmap-zoom-in diaglvl tileiter texteff cargomap multimap reservation warning-sign moving-average selfaware-stationcargo
	git push github $(^F)

