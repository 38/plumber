#!/bin/bash
SRC_DIR=`dirname $0`
PANDOC=pandoc
if [ "${MDFLAGS}" = "" ]
then
	PDFLAGS="--toc --template=${SRC_DIR}/template.txt"
fi
if [ "${SRC_DIR}" = "" ]
then
	SRC_DIR=.
fi
filelist=($(ls ${SRC_DIR}/*.md|sort) )
prevpage=""
for((i=0; i<${#filelist[@]};i++))
do
	file="${filelist[${i}]}"
	nextid=$((${i} + 1))
	if [ "${nextid}" != "${#filelist[@]}" ]
	then
		nextpage=`basename ${filelist[${nextid}]} .md`.html
		nexttitle=`head -n 1 ${filelist[${nextid}]} | awk -F'\t' '{print $1}'`
	else
		nextpage=""
		nexttitle=""
	fi
	numlines=`wc -l ${file} | awk '{print $1}'`
	bodylines=`expr ${numlines} - 1`
	pagename=`basename ${file} .md`
	title=`head -n 1 ${file} | awk -F'\t' '{print $1}'`
	echo "Making page ${pagename}.html"
	tail -n ${bodylines} ${file} | \
	${PANDOC} ${PDFLAGS} -V "title:${title}" -t json |\
	python ${SRC_DIR}/graphviz.py -T png |\
	${PANDOC} ${PDFLAGS} -V "title:${title}" \
	                     -V "prev:${prevpage}" \
						 -V "next:${nextpage}" \
						 -V "ptitle:${prevtitle}" \
						 -V "ntitle:${nexttitle}" \
						 -f json -o "${pagename}.html"
	prevpage="${pagename}.html"
	prevtitle="${title}"
done

cp ${SRC_DIR}/*.css .
