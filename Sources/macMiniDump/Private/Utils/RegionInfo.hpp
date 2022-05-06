// scummed from llvm, MachVMRegion.h and .cpp
#if defined(VM_REGION_SUBMAP_SHORT_INFO_COUNT_64)
  typedef vm_region_submap_short_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 };
#else
  typedef vm_region_submap_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_INFO_COUNT_64 };
#endif