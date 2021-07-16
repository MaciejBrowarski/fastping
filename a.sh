#!/bin/bash
# compile source
#
# History
# 0.1 - create
#
CC=/usr/bin/gcc
OPT="-O2 -lpthread -Wall"
$CC -o ping ping.c $OPT

