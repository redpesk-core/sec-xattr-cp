#!/bin/bash

pl() {
cat << EOC
# file: data/subdata1/file1
user.name0="value0"
user.name1="value1"

# file: data/subdata1/file2
user.name0="value0"
user.name1="value1"

# file: data/subdata1/file3
user.name0="value8"
user.name1="value9"

# file: data/subdata2/file1
user.name0="value0"
user.name1="value1"

# file: data/subdata2
user.name3="value0"
user.name4="value1"

# file: data/subdata3/file4
user.name0="value7"
user.name1="value8"

# file: data/subdata3/file2
user.name0="value9"
user.name1="value1"

# file: data/subdata3/file1
user.name0="value0"
user.name1="value5"

# file: data
user.name1="value5"
EOC
}

ppl() {
	pl |
	sed "s,^# file: ,&$1,"
}

fl() {
	ppl "$1" |
	awk '/^# file:/{print $3}'
}

dl() {
	fl "$1" |
	xargs -n 1 dirname |
	sort -u
}

# create the dirin
rm -r dirin
dl "dirin/" | xargs mkdir -p
fl "dirin/" | xargs touch
ppl "dirin/" | setfattr --restore -

# extract xattr
getfattr -R -d dirin | sed 's,dirin,,' > out.in.fattr
./sec-xattr-extract -d out.extr dirin > out.extr.dump

# create the dirout
rm -rf dirout
dl "dirout/" | xargs mkdir -p
fl "dirout/" | xargs touch
./sec-xattr-restore -d out.extr dirout > out.rest.dump
./sec-xattr-restore out.extr dirout
getfattr -R -d dirout | sed 's,dirout,,' > out.out.fattr

# check
if ! cmp out.out.fattr out.in.fattr
then
	echo "ERROR detected in raw ouput"
	exit 1
fi
