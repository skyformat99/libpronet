/*
 * Copyright (c) 1996-1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

#ifndef __SGI_STL_INTERNAL_ALLOC_H
#define __SGI_STL_INTERNAL_ALLOC_H

// This implements some standard node allocators.  These are
// NOT the same as the allocators in the C++ draft standard or in
// in the original STL.  They do not encapsulate different pointer
// types; indeed we assume that there is only one pointer type.
// The allocation primitives are intended to allocate individual objects,
// not larger arenas as with the original STL allocators.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if !defined(____STD____)
#define ____STD____
#define ____STD_BEGIN namespace std {
#define ____STD_END   };
#endif

____STD_BEGIN

// Default node allocator.
// With a reasonable compiler, this should be roughly as fast as the
// original STL class-specific allocators, but with less fragmentation.
// Default_alloc_template parameters are experimental and MAY
// DISAPPEAR in the future.  Clients should just use alloc for now.
//
// Important implementation properties:
// 1. If the client request an object of size > _MAX_BYTES, the resulting
//    object will be obtained directly from malloc.
// 2. In all other cases, we allocate an object of size exactly
//    _S_round_up(requested_size).  Thus the client has enough size
//    information that we can return the object to the proper free list
//    without permanently losing part of the object.

// It is safe to allocate an object from one instance of a default_alloc
// and deallocate it with another one.  This effectively transfers its
// ownership to the second one.  This may have undesirable effects
// on reference locality.
// The template parameter is unreferenced and serves only to allow the
// creation of multiple default_alloc instances.
// Node that containers built on different allocator instances have
// different types, limiting the utility of this approach.

enum { _MAX_BYTES  = 1024 * 128 };      /* sizeof(obj) <= 128K */
enum { _NFREELISTS = 60 };              /* 60 levels */
enum { _CHUNK_SIZE = 1024 * 1024 * 2 }; /* sizeof(chunk) is 2M, >= glibc::M_MMAP_THRESHOLD */

union _Obj
{
    union _Obj* _M_free_list_link;
    char        _M_client_data[1];
};

template<int inst>
class __default_alloc_template
{
private:

#if defined(__SUNPRO_CC) || defined(__GNUC__) || defined(__HP_aCC)
    static _Obj* volatile _S_free_list[];
    static size_t         _S_obj_size[];
#else
    static _Obj* volatile _S_free_list[_NFREELISTS];
    static size_t         _S_obj_size[_NFREELISTS];
#endif

    static int _S_freelist_index(size_t __bytes)
    {
        int __l = 0, __r = _NFREELISTS - 1;

        if (__bytes <= _S_obj_size[__l])
        {
            return (__l);
        }
        else if (__bytes >= _S_obj_size[__r])
        {
            return (__r);
        }
        else
        {
        }

        while (__l < __r)
        {
            int __m = (__l + __r) / 2;
            if (__bytes == _S_obj_size[__m])
            {
                return (__m);
            }
            else if (__bytes > _S_obj_size[__m])
            {
                __l = __m + 1;
            }
            else
            {
                __r = __m;
            }
        }

        return (__l);
    }

    static size_t _S_round_up(size_t __bytes)
    {
        return (_S_obj_size[_S_freelist_index(__bytes)]);
    }

    // Returns an object of size __n, and optionally adds to size __n free list.
    static void* _S_refill(size_t __n);
    // Allocates a chunk for __nobjs of size __size.  __nobjs may be reduced
    // if it is inconvenient to allocate the requested number.
    static char* _S_chunk_alloc(size_t __size, int& __nobjs);

    // Chunk allocation state.
    static char*  _S_start_free;
    static char*  _S_end_free;
    static size_t _S_heap_size;

public:

    /* __n must be > 0      */
    static void* allocate(size_t __n)
    {
        if (__n == 0)
        {
            return (0);
        }

        void* __ret = 0;

        if (__n > (size_t)_MAX_BYTES)
        {
            __ret = malloc(__n);
        }
        else
        {
            _Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);
            _Obj* __result = *__my_free_list;

            if (__result == 0)
            {
                __ret = _S_refill(_S_round_up(__n));
            }
            else
            {
                *__my_free_list = __result->_M_free_list_link;
                __ret = __result;
            }
        }

        return (__ret);
    }

    /* __p may not be 0 */
    static void deallocate(void* __p, size_t __n)
    {
        if (__p == 0 || __n == 0)
        {
            return;
        }

        if (__n > (size_t)_MAX_BYTES)
        {
            free(__p);
        }
        else
        {
            _Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);
            _Obj* __q = (_Obj*)__p;

            __q->_M_free_list_link = *__my_free_list;
            *__my_free_list = __q;
        }
    }

    static void* reallocate(void* __p, size_t __old_sz, size_t __new_sz)
    {
        if (__p == 0 || __old_sz == 0)
        {
            return (allocate(__new_sz));
        }

        if (__new_sz == 0)
        {
            deallocate(__p, __old_sz);

            return (0);
        }

        if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES)
        {
            return (realloc(__p, __new_sz));
        }

        if (_S_round_up(__old_sz) == _S_round_up(__new_sz))
        {
            return (__p);
        }

        void* __result = allocate(__new_sz);
        if (__result != 0)
        {
            size_t __copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
            memcpy(__result, __p, __copy_sz);
            deallocate(__p, __old_sz);
        }

        return (__result);
    }

    static void get_info(
        void*   __free_list[_NFREELISTS],
        size_t  __obj_size[_NFREELISTS],
        size_t& __heap_size
        )
    {
        memcpy(__free_list, (void*)_S_free_list, sizeof(_S_free_list));
        memcpy(__obj_size, (void*)_S_obj_size, sizeof(_S_obj_size));
        __heap_size = _S_heap_size;
    }
};

/* Returns an object of size __n, and optionally adds to size __n free list.*/
/* We assume that __n is properly aligned.                                  */
template<int __inst>
void*
__default_alloc_template<__inst>::_S_refill(size_t __n)
{
    int             __nobjs        = 20;
    char*           __chunk        = _S_chunk_alloc(__n, __nobjs);
    _Obj* volatile* __my_free_list = 0;
    _Obj*           __result       = 0;
    _Obj*           __current_obj  = 0;
    _Obj*           __next_obj     = 0;

    if (__chunk == 0 || __nobjs == 1)
    {
        return (__chunk);
    }

    __my_free_list = _S_free_list + _S_freelist_index(__n);

    /* Build free list in chunk */
    __result = (_Obj*)__chunk;
    *__my_free_list = __next_obj = (_Obj*)(__chunk + __n);

    for (int __i = 1; ; __i++)
    {
        __current_obj = __next_obj;
        __next_obj = (_Obj*)((char*)__next_obj + __n);

        if (__nobjs - 1 == __i)
        {
            __current_obj->_M_free_list_link = 0;
            break;
        }
        else
        {
            __current_obj->_M_free_list_link = __next_obj;
        }
    }

    return (__result);
}

/* We allocate memory in large chunks in order to avoid fragmenting     */
/* the malloc heap too much.                                            */
/* We assume that __size is properly aligned.                           */
template<int __inst>
char*
__default_alloc_template<__inst>::_S_chunk_alloc(size_t __size,
                                                 int&   __nobjs)
{
    if (__size >= (size_t)4096)
    {
        __nobjs = 1;
    }
    else if (__size * __nobjs >= (size_t)4096)
    {
        __nobjs = (int)(4096 / __size);
    }
    else
    {
    }

    char*  __result      = 0;
    size_t __total_bytes = __size * __nobjs;
    size_t __bytes_left  = _S_end_free - _S_start_free;

    if (__bytes_left >= __total_bytes)
    {
        __result = _S_start_free;
        _S_start_free += __total_bytes;

        return (__result);
    }
    else if (__bytes_left >= __size)
    {
        __nobjs = (int)(__bytes_left / __size);
        __total_bytes = __size * __nobjs;

        __result = _S_start_free;
        _S_start_free += __total_bytes;

        return (__result);
    }
    else
    {
        for (int __i = _S_freelist_index(__bytes_left); __i >= 0; --__i)
        {
            if (__bytes_left == 0)
            {
                break;
            }

            size_t          __obj_size     = _S_obj_size[__i];
            _Obj* volatile* __my_free_list = _S_free_list + __i;

            while (__bytes_left >= __obj_size)
            {
                ((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
                *__my_free_list = (_Obj*)_S_start_free;

                _S_start_free += __obj_size;
                __bytes_left  -= __obj_size;
            }
        }

        size_t __bytes_to_get = _CHUNK_SIZE;

        _S_start_free = (char*)malloc(__bytes_to_get);
        _S_end_free   = 0;
        if (_S_start_free == 0)
        {
            __bytes_to_get = _CHUNK_SIZE / 2;
            _S_start_free  = (char*)malloc(__bytes_to_get); /* retry */
        }

        if (_S_start_free != 0)
        {
            _S_end_free  =  _S_start_free + __bytes_to_get;
            _S_heap_size += __bytes_to_get;

            return (_S_chunk_alloc(__size, __nobjs));
        }

        // Try to make do with what we have.  That can't
        // hurt.  We do not try smaller requests, since that tends
        // to result in disaster on multi-process machines.
        for (int __j = _S_freelist_index(__size); __j < _NFREELISTS; ++__j)
        {
            _Obj* volatile* __my_free_list = _S_free_list + __j;
            if (*__my_free_list != 0)
            {
                _Obj* __p = *__my_free_list;
                *__my_free_list = __p->_M_free_list_link;

                _S_start_free = (char*)__p;
                _S_end_free   = _S_start_free + _S_obj_size[__j];

                return (_S_chunk_alloc(__size, __nobjs));
            }
        }

        return (0);
    }
}

template<int __inst>
char* __default_alloc_template<__inst>::_S_start_free = 0;

template<int __inst>
char* __default_alloc_template<__inst>::_S_end_free   = 0;

template<int __inst>
size_t __default_alloc_template<__inst>::_S_heap_size = 0;

template<int __inst>
_Obj* volatile
__default_alloc_template<__inst>::_S_free_list[_NFREELISTS] =
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/*
 * reference to the "jemalloc/tcmalloc"
 */
template<int __inst>
size_t
__default_alloc_template<__inst>::_S_obj_size[_NFREELISTS] =
{    8,
    16,   32,   48,   64, 80, 96, 112, 128,
   160,  192,  224,  256,
   320,  384,  448,  512,
   640,  768,  896, 1024,
  1280, 1536, 1792, 2048,
  2560, 3072, 3584,
  4096 *  1, 4096 *  2, 4096 *  3, 4096 *  4, 4096 *  5, 4096 *  6, 4096 *  7, 4096 *  8, 4096 *  9, 4096 * 10,
  4096 * 11, 4096 * 12, 4096 * 13, 4096 * 14, 4096 * 15, 4096 * 16, 4096 * 17, 4096 * 18, 4096 * 19, 4096 * 20,
  4096 * 21, 4096 * 22, 4096 * 23, 4096 * 24, 4096 * 25, 4096 * 26, 4096 * 27, 4096 * 28, 4096 * 29, 4096 * 30,
  4096 * 31, 4096 * 32 };

____STD_END

#endif /* __SGI_STL_INTERNAL_ALLOC_H */

// Local Variables:
// mode:C++
// End:
