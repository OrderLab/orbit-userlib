#!/bin/bash

file=forktest-$(date +%s).log

for i in {0..9}; do
	echo test$i >> $file
	./forktest >> $file
	sleep 1
	kill `pidof forktest`
	sleep 1
done
