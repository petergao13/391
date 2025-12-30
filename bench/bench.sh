make clean
make all
../util/mkfs_ktfs ../sys/ktfs.raw 8M 32 ../usr/bin/shell ../bench/bin/bench_fs ../bench/bin/bench_proc ../bench/bin/bench_pipe ../bench/bin/bench_syscall ../bench/bin/bench_mem ../bench/bin/bench_cache
cd ../usr
make clean
make all
cd ../sys
make clean
make run