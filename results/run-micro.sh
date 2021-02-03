#!/bin/bash

#file=micro-async-$(date +%s).log
file=micro-sync-$(date +%s).log

free -h

for i in {0..9}; do
	echo test$i >> $file
	#./micro -a >> $file
	./micro >> $file
	sleep 1
	free -h
done
