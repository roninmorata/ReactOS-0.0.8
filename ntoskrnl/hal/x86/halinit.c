/*
 * COPYRIGHT:     See COPYING in the top level directory
 * PROJECT:       ReactOS kernel
 * FILE:          mkernel/hal/x86/halinit.c
 * PURPOSE:       Initalize the uniprocessor, x86 hal
 * PROGRAMMER:    David Welch (welch@mcmail.com)
 * UPDATE HISTORY:
 *              11/06/98: Created
 */

/* INCLUDES *****************************************************************/

#include <internal/kernel.h>
#include <internal/hal/hal.h>

/* FUNCTIONS ***************************************************************/

VOID HalInit(boot_param* bp)
{
   
   InitalizeExceptions();
   InitalizeIRQ();
   
   /*
    * Probe for a BIOS32 extension
    */
   Hal_bios32_probe();
   
   /*
    * Probe for buses attached to the computer
    * NOTE: Order is important here because ISA is the default
    */
   #if 0
   if (HalPciProbe())
   {
        return;
   }
   HalIsaProbe();
   #endif
}



