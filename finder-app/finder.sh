#!/bin/sh

FILESDIR="$1"
SEARCHSTR="$2"

if [ -z "${FILESDIR}" ] || [ -z "${SEARCHSTR}" ];
  then
	
	echo "You must specify the file directory and search string"
	exit 1
fi

if [ -d "${FILESDIR}" ]; then

	X=`find "${FILESDIR}" -type f | wc -l`
	Y=`grep -rnw "${FILESDIR}" -e "${SEARCHSTR}" | wc -l`

	echo "The number of files are ${X} and the number of matching lines are ${Y}"
else
  	echo " Does not rapresent a file directory in the filesystem"
	exit 1
fi

