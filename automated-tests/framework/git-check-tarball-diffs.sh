#!/usr/bin/env bash

set -o errexit
set -o nounset
#set -o xtrace

LOGFILE="/tmp/git-diff-tarball.txt"

git diff --name-only | grep --color=none '\.tar.gz' > $LOGFILE

for orig_tarball in $(cat $LOGFILE); do
	orig_tarball=$(git rev-parse --show-toplevel)/$orig_tarball
	new_tarball=$orig_tarball.tar.gz

	mv $orig_tarball $new_tarball
	git checkout $orig_tarball

	new_output_dir="/tmp/git-diff-new"
	orig_output_dir="/tmp/git-diff-orig"
	
	rm -rf $new_output_dir
	rm -rf $orig_output_dir

	mkdir -p $new_output_dir
	mkdir -p $orig_output_dir

	tar -C $new_output_dir  -xzvf $new_tarball  > /dev/null
	tar -C $orig_output_dir -xzvf $orig_tarball > /dev/null

	diff -ur $orig_output_dir $new_output_dir | less

	echo
	echo -n "$orig_tarball: discard changes? [Y/n] "
	read ans

	if [ -z "$ans" ]; then
		rm -f $new_tarball
	else
		mv $new_tarball $orig_tarball
	fi
done
