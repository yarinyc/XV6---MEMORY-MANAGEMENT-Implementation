typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;
typedef uint pte_t;

// task 4: glonal struct for counting all physical pages in the system
struct gloabl_meta_data{
  uint total_system_pages;
  uint system_free_pages;
};

extern struct gloabl_meta_data gloabl_memory_meta_data; //export this variable so it's global in the system