#ifndef _TESTSUITE_1_H_
#define _TESTSUITE_1_H_

// Add more test prototypes here
// Add args if you want
void run_testsuite_1(void);
// given testcases
int test_find_storage();
int test_simple_storage_read();
int test_simple_storage_write();
int test_simple_ramdisk_uio_read();
int test_uio_control_ramdisk_read();
int test_elf_load_with_ramdisk_uio();
int test_cache_get_and_release_block();
int test_cache_get();
int testMultipleWrite();
int testCacheEvict();
int testGoon();
int goonEntry1();
int goonEntry2();
int goonEntry3();
int goonEntry4();
int goonEntry5();
int goonEntry6();
int goonEntry7();
int goonEntry8();
int goonEntry9();
int goonEntry10();

#endif // _TESTSUITE_1_H_