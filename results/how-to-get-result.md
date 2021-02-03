# How to get result

## 2021.02.03

**forktest**

This version of forktest did not count wait thread joining time, so recalculate with:

```bash
awk 'NR % 4 >= 2 {sum+=$1}END{printf "%.2f\n", 100000*NR/4/(sum/1000000000)}' forktest-1612387485.log
```

**micro-sync**

```bash
grep checker micro-sync-1612389950.log | awk '{sum+=$6}END{printf "%.2f\n", 1000000*NR/sum*1000000000}'
```

**micro-async**

```bash
grep checker micro-async-1612388185.log | awk '{sum+=$8}END{printf "%.2f\n", sum/NR}'
```
