// Copyright (C) 2018 Mateusz 'DevSH' Kielan
// This file is part of the "IrrlichtBAW Engine"
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef __IRR_CONTIGUOUS_POOL_ADDRESS_ALLOCATOR_H_INCLUDED__
#define __IRR_CONTIGUOUS_POOL_ADDRESS_ALLOCATOR_H_INCLUDED__

#include "IrrCompileConfig.h"

#include "irr/core/alloc/PoolAddressAllocator.h"

namespace irr
{
namespace core
{


//! Can only allocate up to a size of a single block, no support for allocations larger than blocksize
template<typename _size_type>
class ContiguousPoolAddressAllocator : protected PoolAddressAllocator<_size_type>
{
    private:
        typedef PoolAddressAllocator<_size_type>    Base;
    public:
        _IRR_DECLARE_ADDRESS_ALLOCATOR_TYPEDEFS(_size_type);

        static constexpr bool supportsNullBuffer = false;
        static constexpr uint32_t maxMultiOps = 4096u;

        GCC_CONSTRUCTOR_INHERITANCE_BUG_WORKAROUND(ContiguousPoolAddressAllocator() : addressRedirects(nullptr) {})

        virtual ~ContiguousPoolAddressAllocator() {}

        ContiguousPoolAddressAllocator(void* reservedSpc, void* buffer, size_type bufSz, size_type blockSz) noexcept :
                                PoolAddressAllocator<_size_type>(reservedSpc,buffer,bufSz,blockSz),
                                addressRedirects(reinterpret_cast<size_type*>(Base::reservedSpace)+Base::blockCount)
        {
            selfOnlyReset();
        }
        ContiguousPoolAddressAllocator(const ContiguousPoolAddressAllocator& other, void* newReservedSpc, void* newBuffer, size_type newBuffSz) noexcept :
                                PoolAddressAllocator<_size_type>(other,newReservedSpc,newBuffer,newBuffSz),
                                addressRedirects(reinterpret_cast<size_type*>(Base::reservedSpace)+Base::blockCount),
                                addressesAllocated(other.addressesAllocated)
        {
            memmove(addressRedirects,other.addressRedirects,Base::blockCount*sizeof(size_type));
        }

        // extra
        inline size_type        get_real_addr(size_type allocated_addr) const
        {
            return addressRedirects[allocated_addr];
        }

        // extra
        inline void             multi_free_addr(uint32_t count, const size_type* addr, const size_type* bytes) noexcept
        {
            if (count==0)
                return;
#ifdef _DEBUG
            assert(freeStackCtr<=blockCount+count);
#endif // _DEBUG

            size_type sortedRedirects[maxMultiOps];
            size_type sortedRedirectsEnd = sortedRedirects+count;
            for (decltype(count) i=0; i<count; i++)
            {
                auto tmp  = addr[i];
#ifdef _DEBUG
                assert(tmp>=alignOffset);
#endif // _DEBUG
                reinterpret_cast<size_type*>(Base::reservedSpace)[Base::freeStackCtr++] = tmp;
                auto redir = addressRedirects[tmp];
#ifdef _DEBUG
                assert(redir<addressesAllocated);
#endif // _DEBUG
                sortedRedirects[i] = redir;
            }
            std::make_heap(sortedRedirects,sortedRedirectsEnd);
            std::sort_heap(sortedRedirects,sortedRedirectsEnd);


            // shift redirects
            for (size_t i=0; i<Base::blockCount; i++)
            {
                size_type rfrnc = addressRedirects[i];
                if (rfrnc>=invalid_address)
                    continue;

                size_type* ptr = std::lower_bound(sortedRedirects,sortedRedirectsEnd,rfrnc);
                if (ptr<(sortedRedirectsEnd)&&ptr[0]==rfrnc)
                    addressRedirects[i] = invalid_address;
                else
                {
                    size_type difference = ptr-sortedRedirects;
                    addressRedirects[i] = rfrnc-difference;
                }
            }
#ifdef _DEBUG
			assert(deletedGranuleCnt==count);
#endif // _DEBUG

            if (Base::bufferStart)
            {
                for (decltype(count) i=0; i<count; i++)
                    sortedRedirects[i] *= Base::blockSize;

                decltype(count) nextIx=1;
                decltype(count) j=0;
                while (nextIx<count)
                {
                    ubyte_pointer oldRedirectedAddress = reinterpret_cast<ubyte_pointer>(Base::bufferStart)+sortedRedirects[j];
                    size_t len = sortedRedirects[nextIx]-sortedRedirects[j]-Base::blockSize;
                    if (len)
                        memmove(oldRedirectedAddress-j*Base::blockSize,oldRedirectedAddress+Base::blockSize,len);

                    j = nextIx++;
                }
                size_type len = addressesAllocated-sortedRedirects[j]-Base::blockSize;
                if (len)
                {
                    ubyte_pointer oldRedirectedAddress = reinterpret_cast<ubyte_pointer>(Base::bufferStart)+sortedRedirects[j];
                    memmove(oldRedirectedAddress-j*Base::blockSize,oldRedirectedAddress+Base::blockSize,len);
                }
            }
            addressesAllocated -= count;
        }

        inline size_type        alloc_addr(size_type bytes, size_type alignment, size_type hint=0ull) noexcept
        {
            auto ID = Base::alloc_addr(bytes,alignment,hint);
            addressRedirects[ID] = addressesAllocated++;
            return ID;
        }

        inline void             free_addr(size_type addr, size_type bytes) noexcept
        {
            multi_free_addr(1u,&addr,&bytes);
        }

        inline void             reset()
        {
            Base::reset();
            selfOnlyReset();
        }


        inline size_type        safe_shrink_size(size_type bound=0u) const noexcept
        {
            if (addressesAllocated*Base::blockSize>bound)
                bound = addressesAllocated*Base::blockSize;

            size_type newBound = Base::blockCount;
            for (; newBound*Base::blockSize>bound; newBound--)
            {
                if (addressRedirects[newBound-1u]<invalid_address)
                    break;
            }
            bound = newBound*Base::blockSize;

            return Base::safe_shrink_size(bound);
        }


        template<typename... Args>
        static inline size_type reserved_size(size_type bufSz, size_type blockSz, Args&&... args) noexcept
        {
            size_type trueAlign = size_type(1u)<<findLSB(blockSz);
            size_type truncatedOffset = calcAlignOffset(0x8000000000000000ull-size_t(_IRR_SIMD_ALIGNMENT),trueAlign);
            size_type probBlockCount =  (bufSz-truncatedOffset)/(blockSz+2ull*sizeof(size_type))+1u;
            return probBlockCount*sizeof(size_type);
        }

    protected:
        size_type* const                        addressRedirects;
        size_type                               addressesAllocated;
    private:
        inline void selfOnlyReset()
        {
            for (size_type i=0ull; i<Base::blockCount; i++)
                addressRedirects[i] = invalid_address;
            addressesAllocated = 0ull;
        }
};


// aliases
template<typename size_type>
using ContiguousPoolAddressAllocatorST = ContiguousPoolAddressAllocator<size_type>;

template<typename size_type, class BasicLockable>
using ContiguousPoolAddressAllocatorMT = AddressAllocatorBasicConcurrencyAdaptor<ContiguousPoolAddressAllocator<size_type>,BasicLockable>;

}
}

#endif // __IRR_CONTIGUOUS_POOL_ADDRESS_ALLOCATOR_H_INCLUDED__

