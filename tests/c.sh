#!/usr/bin/env bash
file="test.c"
out="a.out"
if [ $# -eq 1 ];then
  file=$1
elif [ $# -eq 2 ];then
  file=$1
  out=$2
fi


echo "编译 $file 为$out"
cc -o $out $file
echo "执行 $out"
./$out
