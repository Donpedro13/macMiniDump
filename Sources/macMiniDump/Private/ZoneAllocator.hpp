#ifndef MMD_ZONEALLOCATOR
#define MMD_ZONEALLOCATOR

#pragma once

// The primary use-case for creating memory dumps is crash reporting. However, running code in a crashing
//   process/thread is dangerous, as its state might be corrupt. In other words: if not careful, running code in this
//   context will just cause a crash while handling/reporting the crash. There is no solution to this, just avoidance:
//   creating a memory dump from a different process (out-of-process crash reporting). However, since that is a lot of
//   work and complexity, this library strives to be as resilient as possible even if used in-process. A cornerstone of
//   this is using a dedicated malloc zone (~heap) for all allocations done by this library. This way, we survive most
//   heap corruptions.

#include <malloc/malloc.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MMD {

malloc_zone_t* GetZone ();

void* Malloc (size_t size);
void* MallocAligned (size_t size, size_t alignment);
void  Free (void* ptr);

// Custom allocator that uses the dedicated malloc zone
template<typename T>
class ZoneAllocator {
public:
	using value_type	  = T;
	using size_type		  = std::size_t;
	using difference_type = std::ptrdiff_t;

	ZoneAllocator () noexcept = default;

	template<typename U>
	ZoneAllocator (const ZoneAllocator<U>&) noexcept
	{
	}

	T* allocate (std::size_t n)
	{
		void* p = malloc_zone_malloc (GetZone (), n * sizeof (T));
		if (p == nullptr)
			throw std::bad_alloc ();

		return static_cast<T*> (p);
	}

	void deallocate (T* p, std::size_t) noexcept { malloc_zone_free (GetZone (), p); }

	template<typename U>
	bool operator== (const ZoneAllocator<U>&) const noexcept
	{
		return true;
	}

	template<typename U>
	bool operator!= (const ZoneAllocator<U>&) const noexcept
	{
		return false;
	}
};

using String = std::basic_string<char, std::char_traits<char>, ZoneAllocator<char>>;

template<typename T>
using Vector = std::vector<T, ZoneAllocator<T>>;

template<typename K, typename V, typename Compare = std::less<K>>
using Map = std::map<K, V, Compare, ZoneAllocator<std::pair<const K, V>>>;

// Custom deleter for use with std::unique_ptr
template<typename T>
struct ZoneDeleter {
	void operator() (T* ptr) const noexcept
	{
		if (ptr) {
			ptr->~T ();
			Free (ptr);
		}
	}
};

// Specialization for arrays
template<typename T>
struct ZoneDeleter<T[]> {
	void operator() (T* ptr) const noexcept { Free (ptr); }
};

template<typename T>
using UniquePtr = std::unique_ptr<T, ZoneDeleter<T>>;

template<typename T, typename... Args>
UniquePtr<T> MakeUnique (Args&&... args)
{
	void* p = Malloc (sizeof (T));
	if (p == nullptr)
		throw std::bad_alloc ();

	return UniquePtr<T> (new (p) T (std::forward<Args> (args)...));
}

template<typename T>
UniquePtr<T[]> MakeUniqueArray (size_t count)
{
	void* p = Malloc (count * sizeof (T));
	if (p == nullptr)
		throw std::bad_alloc ();

	return UniquePtr<T[]> (static_cast<T*> (p));
}

template<typename T>
UniquePtr<T[]> MakeUniqueArrayAligned (size_t count, size_t alignment)
{
	void* p = MallocAligned (count * sizeof (T), alignment);
	if (p == nullptr)
		throw std::bad_alloc ();

	return UniquePtr<T[]> (static_cast<T*> (p));
}

// Base class that provides zone-aware new/delete operators
// Inherit from this class to make allocations use the dedicated malloc zone
class ZoneAllocated {
public:
	static void* operator new (size_t size);
	static void* operator new (size_t size, std::align_val_t alignment);
	static void	 operator delete (void* ptr) noexcept;
	static void	 operator delete (void* ptr, std::align_val_t) noexcept;

	static void* operator new[] (size_t size);
	static void	 operator delete[] (void* ptr) noexcept;

	virtual ~ZoneAllocated () = default;
};

} // namespace MMD

#endif // MMD_ZONEALLOCATOR
