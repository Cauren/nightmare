#include <fstream>
#include <sstream>
#include <vector>
#include "cpu.hh"

namespace Nightmare {


void CPU::oscall(void)
{
    ccr&C = false;

    // Ugly hack for kmalloc/kfree:
    // we manage the heap "outside" of the CPU in a really naive way
    // by just looking for the first free set of consecutive 4k pages 
    // with a dumb linear search.  Good enough for now.
    //
    // If len<0 then this is part of allocation for page -len
    // If len>0 then this is a len page allocation
    // if len==0 then the page is free
    static std::vector<int64_t> heap = { 2, 1 }; // first two pages are never available
    static constexpr size_t page = 4*1024;

    switch(d[0].data) {

      case 0: {	// kmalloc
	  size_t    wants = (d[1].data+page-1) / page;
	  size_t    pfree = 0;
	  size_t    nfree = 0;
	  size_t    max = heap.size();

	  size_t    p = 1;
	  while(p<max && nfree<wants) {
	      if(heap[p] > 0) {
		  p += heap[p];
		  nfree = 0;
		  continue;
	      }
	      pfree = p;
	      while(p<max && heap[p]==0)
		  p++, nfree++;
	  }
	  if(pfree+nfree != max) {
	      pfree = max;
	      nfree = 0;
	  }
	  if(nfree<wants) {
	      if((pfree+wants)/page > mem_alloc_) {
		  d[0].data = signed_<36>(ENOMEM);
		  break;
	      }
	      heap.resize(pfree+wants);
	  }
	  for(p=pfree+1; p<pfree+wants; p++)
	      heap[p] = -pfree;
	  heap[pfree] = wants;
	  a[0].seg = 0777777;
	  a[0].addr = pfree*page;
	  return;

      } break;

      case 1: {	// kfree
      } break;

    }

    ccr&C = true;
}



}; // namespace Nightmare

