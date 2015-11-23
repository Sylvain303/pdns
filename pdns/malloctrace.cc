#include "malloctrace.hh"
#include <sstream>
#include <algorithm>

using std::string;
using std::map;
using std::vector;

MallocTracer* g_mtracer;

#if 1
#include <execinfo.h>
vector<void*> MallocTracer::makeBacktrace()
{
  void *array[20]; //only care about last 17 functions (3 taken with tracing support)
  size_t size = backtrace (array, 20);
  return vector<void*>(array, array+size);
}


extern "C" {
extern void *__libc_malloc(size_t size);
extern void __libc_free(void* ptr);

void* malloc (size_t size)
{
  if(!g_mtracer) {
    void *mem = __libc_malloc(sizeof(MallocTracer));
    g_mtracer = new(mem) MallocTracer;
  }
  return g_mtracer->malloc(size);
}

void free(void* ptr)
{
  if(ptr)
    g_mtracer->free(ptr);
}
}

#endif

static thread_local bool l_active;
void* MallocTracer::malloc(size_t size)
{
  void* ret = __libc_malloc(size);
  if(!l_active) {
    l_active=true;

    d_allocflux+=size;
    d_allocs++;
    d_totAllocated += size;


    std::lock_guard<std::mutex> lock(d_mut);
    auto& ent=d_stats[makeBacktrace()];
    ent.count++;
    ent.sizes[size]++;
    d_sizes[ret]=size;

    l_active=false;
  }
  return ret;
}

void MallocTracer::free(void* ptr)
{
  __libc_free(ptr);
  if(!l_active) {
    l_active=true;
    std::lock_guard<std::mutex> lock(d_mut);
    auto f = d_sizes.find(ptr);
    if(f != d_sizes.end()) {
      d_totAllocated -= f->second;
      d_sizes.erase(f);
    }
    l_active=false;
  }
}

MallocTracer::allocators_t MallocTracer::topAllocators(int num)
{
  l_active=true;
  allocators_t ret;
  for(const auto& e : d_stats) {
    ret.push_back(make_pair(e.second, e.first));
  }
  std::sort(ret.begin(), ret.end(), 
       [](const allocators_t::value_type& a, 
	  const allocators_t::value_type& b) {
	 return a.first.count < b.first.count;
       });
  if(num > 0)
    ret.erase(ret.begin(), ret.begin() + (ret.size() - num));
  l_active=false;
  return ret;
}

std::string MallocTracer::topAllocatorsString(int num)
{
  l_active=true;
  auto raw = topAllocators(num);
  l_active=true;
  std::ostringstream ret;
  for(const auto& e : raw) {
    ret<<"Called "<<e.first.count<<" times\n";
    for(const auto& u : e.first.sizes) 
      ret<<u.first<<"b: "<<u.second<<" times, ";
    ret<<'\n';
    char** strings = backtrace_symbols(&e.second[0], e.second.size());
    for(unsigned int i=0; i < e.second.size(); ++i)
      ret<<strings[i]<<'\n';
    ret<<"-----\n";
  }

  string str =ret.str();
  l_active=false;
  return str;
}

/*
char **strings;
  size_t i;
  strings = backtrace_symbols (array, size); //Need -rdynamic gcc (linker) flag for this to work

  for (i = 0; i < size; i++) //skip useless functions
    ret+=strings[i]+string("\n");
  return ret;
*/
