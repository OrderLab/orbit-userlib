# Deadlock Detector

## Overview
A test program to mimic mySQL's deadlock detection algorithm. The program runs in an infinite loop and periodically invokes the deadlock detection algorithm (currently once every 5 seconds on average). Work is simulated through thread sleep.

## Data Model
The underlying data model is a simplified one. A global vector of transactions is assumed to exist and each transaction holds a vector of locks. These can be configured through `NUM_TRANSACTIONS` and `NUM_LOCKS` respectively. 

## Compiling
`g++ -std=c++11 deadlock-detector-test.cc -o deadlock-detector-test`