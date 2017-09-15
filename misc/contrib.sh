#!/bin/zsh
for file in $(git ls-files)
do
git blame ${file} | awk -F'(' '{print $2}' | sed 's/\([A-Za-z]* [A-Za-z]*\).*/\1/g'
done | sort  | uniq -c

