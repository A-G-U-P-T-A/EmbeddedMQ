emq_add_suite(fault_alloc fault_alloc.c fault)
emq_add_suite(fault_disk fault_disk.c fault)
emq_add_suite(fault_corrupt fault_corrupt.c fault)

if(NOT EMQ_FAULT_INJECT)
  set_tests_properties(fault_alloc fault_disk PROPERTIES DISABLED TRUE)
endif()
