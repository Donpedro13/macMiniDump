#include "ZoneAllocator.hpp"

#include <cassert>

#include "Utils/Logging.hpp"

namespace MMD {

namespace {

malloc_zone_t* g_zone = nullptr;

__attribute__ ((constructor)) void InitZone ()
{
	malloc_zone_t* zone = malloc_create_zone (0, 0);
	if (zone == nullptr) {
		MMD_DEBUGLOG_LINE << "Failed to create dedicated malloc zone, falling back to default zone";
		g_zone = malloc_default_zone ();

		return;
	}

	malloc_set_zone_name (zone, "MMDZone");
	g_zone = zone;
}

__attribute__ ((destructor)) void FreeZone ()
{
	assert (g_zone != nullptr);

	if (g_zone != malloc_default_zone ())
		malloc_destroy_zone (g_zone);

	g_zone = nullptr;
}

} // namespace

malloc_zone_t* GetZone ()
{
	assert (g_zone != nullptr);

	return g_zone;
}

void* Malloc (size_t size)
{
	return malloc_zone_malloc (GetZone (), size);
}

void* MallocAligned (size_t size, size_t alignment)
{
	return malloc_zone_memalign (GetZone (), alignment, size);
}

void Free (void* ptr)
{
	malloc_zone_free (GetZone (), ptr);
}

void* ZoneAllocated::operator new (size_t size)
{
	return Malloc (size);
}

void* ZoneAllocated::operator new (size_t size, std::align_val_t alignment)
{
	return MallocAligned (size, static_cast<size_t> (alignment));
}

void ZoneAllocated::operator delete (void* ptr) noexcept
{
	Free (ptr);
}

void ZoneAllocated::operator delete (void* ptr, std::align_val_t) noexcept
{
	Free (ptr);
}

void* ZoneAllocated::operator new[] (size_t size)
{
	return Malloc (size);
}

void ZoneAllocated::operator delete[] (void* ptr) noexcept
{
	Free (ptr);
}

} // namespace MMD
