/*
    Copyright � 2017, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <asm/cpu.h>
#include <aros/atomic.h>
#include <aros/types/spinlock_s.h>
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <utility/hooks.h>

#include <kernel_base.h>
#include <kernel_debug.h>

#include <proto/kernel.h>

#define D(x)

AROS_LH3(spinlock_t *, KrnSpinLock,
	AROS_LHA(spinlock_t *, lock, A1),
	AROS_LHA(struct Hook *, failhook, A0),
	AROS_LHA(ULONG, mode, D0),
	struct KernelBase *, KernelBase, 43, Kernel)
{
    AROS_LIBFUNC_INIT

    D(bug("[Kernel] %s(0x%p, 0x%p, %08x)\n", __func__, lock, failhook, mode));

    if (mode == SPINLOCK_MODE_WRITE)
    {
        /*
        Check if lock->lock equals to SPINLOCK_UNLOCKED. If yes, it will be atomicaly replaced by SPINLOCKF_WRITE and function
        returns 1. Otherwise it copies value of lock->lock into tmp and returns 0.
        */
        while (!compare_and_exchange_long((ULONG*)&lock->lock, SPINLOCK_UNLOCKED, SPINLOCKF_WRITE, NULL)) 
        {
            // Tell CPU we are spinning
            asm volatile("pause");

            // Call failhook if there is any
            if (failhook)
            {
               D(bug("[Kernel] %s: lock-held ... calling fail hook @ 0x%p ...\n", __func__, failhook);)
                CALLHOOKPKT(failhook, (APTR)lock, 0);
            }
            D(bug("[Kernel] %s: spinning on held lock ...\n", __func__);)
        };
    }
    else
    {
        /*
        Check if upper 8 bits of lock->lock are all 0, which means spinlock is not in UPDATING state and it is not
        in the WRITE state. If we manage to obtain it, we set the UPDATING flag. Until we release UPDATING state
        we are free to do whatever we want with the spinlock
        */
        while (!compare_and_exchange_byte((UBYTE*)&lock->block[3], 0, SPINLOCKF_UPDATING >> 24, NULL))
        {
            // Tell CPU we are spinning
            asm volatile("pause");

            // Call fail hook if available
            if (failhook)
            {
                D(bug("[Kernel] %s: write-locked .. calling fail hook @ 0x%p ...\n", __func__, failhook);)
                CALLHOOKPKT(failhook, (APTR)lock, 0);
            }
            D(bug("[Kernel] %s: spinning on write lock ...\n", __func__);)
        };
        /*
        At this point we have the spinlock in UPDATING state. So, update readcount field (no worry with atomic add,
        spinlock is for our exclusive use here), and then release it just by setting updating flag to 0
        */
        lock->slock.readcount++;
#if defined(AROS_NO_ATOMIC_OPERATIONS)
        lock->slock.updating = 0;
#else
        __AROS_ATOMIC_AND_L(lock->lock, ~SPINLOCKF_UPDATING);
#endif
    }

    D(bug("[Kernel] %s: lock = %08x\n", __func__, lock->lock));

    return lock;

    AROS_LIBFUNC_EXIT
}