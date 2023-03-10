/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            mkernel/hal/x86/irq.c
 * PURPOSE:         IRQ handling
 * PROGRAMMER:      David Welch (welch@mcmail.com)
 * UPDATE HISTORY:
 *             29/05/98: Created                  
 */

/*
 * NOTE: In general the PIC interrupt priority facilities are used to 
 * preserve the NT IRQL semantics, global interrupt disables are only used
 * to keep the PIC in a consistent state
 *
 */

/* INCLUDES ****************************************************************/

#include <ddk/ntddk.h>

#include <internal/stddef.h>
#include <internal/kernel.h>
#include <internal/bitops.h>
#include <internal/linkage.h>
#include <internal/string.h>

#include <internal/hal/segment.h>
#include <internal/hal/io.h>

#define NDEBUG
#include <internal/debug.h>

/* GLOBALS *****************************************************************/

#define NR_IRQS         (16)
#define IRQ_BASE        (0x20)

asmlinkage void irq_handler_0(void);
asmlinkage void irq_handler_1(void);
asmlinkage void irq_handler_2(void);
asmlinkage void irq_handler_3(void);
asmlinkage void irq_handler_4(void);
asmlinkage void irq_handler_5(void);
asmlinkage void irq_handler_6(void);
asmlinkage void irq_handler_7(void);
asmlinkage void irq_handler_8(void);
asmlinkage void irq_handler_9(void);
asmlinkage void irq_handler_10(void);
asmlinkage void irq_handler_11(void);
asmlinkage void irq_handler_12(void);
asmlinkage void irq_handler_13(void);
asmlinkage void irq_handler_14(void);
asmlinkage void irq_handler_15(void);

static unsigned int irq_handler[NR_IRQS]=
        {
                (int)&irq_handler_0,
                (int)&irq_handler_1,
                (int)&irq_handler_2,
                (int)&irq_handler_3,
                (int)&irq_handler_4,
                (int)&irq_handler_5,
                (int)&irq_handler_6,
                (int)&irq_handler_7,
                (int)&irq_handler_8,
                (int)&irq_handler_9,
                (int)&irq_handler_10,
                (int)&irq_handler_11,
                (int)&irq_handler_12,
                (int)&irq_handler_13,
                (int)&irq_handler_14,
                (int)&irq_handler_15,
        };

/*
 * PURPOSE: Object describing each isr 
 * NOTE: The data in this table is only modified at passsive level but can
 * be accessed at any irq level.
 */
static LIST_ENTRY isr_table[NR_IRQS]={{NULL,NULL},};
static PKSPIN_LOCK isr_lock[NR_IRQS];
static KSPIN_LOCK isr_table_lock;

/* FUNCTIONS ****************************************************************/


#define PRESENT (0x8000)
#define I486_INTERRUPT_GATE (0xe00)

asmlinkage void KiInterruptDispatch(unsigned int irq)
/*
 * FUNCTION: Calls the irq specific handler for an irq
 * ARGUMENTS:
 *         irq = IRQ that has interrupted 
 */
{
   KIRQL old_level;
   PKINTERRUPT isr;
   PLIST_ENTRY current;

   /*
    * Notify the rest of the kernel of the raised irq level
    */
   old_level = KeGetCurrentIrql();
   DPRINT("old_level %d\n",old_level);
   KeSetCurrentIrql(HIGH_LEVEL - irq);
   
   /*
    * Enable interrupts
    * NOTE: Only higher priority interrupts will get through
    */
   __asm__("sti\n\t");

   if (irq==0)
   {
        KeTimerInterrupt();
   }
   else
   {
   DPRINT("KiInterruptDispatch(irq %x)\n",irq);
   /*
    * Iterate the list until one of the isr tells us its device interrupted
    */
   current = isr_table[irq].Flink;
   isr = CONTAINING_RECORD(current,KINTERRUPT,Entry);
   DPRINT("current %x isr %x\n",current,isr);
   while (current!=NULL && !isr->ServiceRoutine(isr,isr->ServiceContext))
     {
        current = current->Flink; 
        isr = CONTAINING_RECORD(current,KINTERRUPT,Entry);
        DPRINT("current %x isr %x\n",current,isr);
     }
   }
   
   /*
    * Disable interrupts
    */
   __asm__("cli\n\t");
   
   /*
    * Send EOI to the PIC
    */
   outb(0x20,0x20);
   if (irq>=8)
     {
	outb(0xa0,0x20);
     }
   
   /*
    * Unmask the related irq
    */
   if (irq<8)
     {
	outb(0x21,inb(0x21)&(~(1<<irq)));
     }
   else
     {
	outb(0xa1,inb(0xa1)&(~(1<<(irq-8))));
     }
   
   /*
    * If the processor level will drop below dispatch level on return then
    * issue a DPC queue drain interrupt
    */
   if (old_level < DISPATCH_LEVEL)
     {
	KeSetCurrentIrql(DISPATCH_LEVEL);
	__asm__("sti\n\t");
	KiDispatchInterrupt(irq);
     }
   KeSetCurrentIrql(old_level);
}

void InitalizeIRQ(void)
{
   int i;
   
   /*
    * First mask off all interrupts from pic
    */
   outb(0x21,0xff);
   outb(0xa1,0xff);
   
   
   /*
    * Setup the IDT entries to point to the interrupt handlers
    */
   for (i=0;i<NR_IRQS;i++)
     {
	idt[IRQ_BASE+i].a=(irq_handler[i]&0xffff)+(KERNEL_CS<<16);
	idt[IRQ_BASE+i].b=(irq_handler[i]&0xffff0000)+PRESENT+
	                  I486_INTERRUPT_GATE;
	InitializeListHead(&isr_table[i]);
     }
}

NTSTATUS IoConnectInterrupt(PKINTERRUPT* InterruptObject,
			    PKSERVICE_ROUTINE ServiceRoutine,
			    PVOID ServiceContext,
			    PKSPIN_LOCK SpinLock,
			    ULONG Vector,
			    KIRQL Irql,
			    KIRQL SynchronizeIrql,
			    KINTERRUPT_MODE InterruptMode,
			    BOOLEAN ShareVector,
			    KAFFINITY ProcessorEnableMask,
			    BOOLEAN FloatingSave)
/*
 * FUNCTION: Registers a driver's isr to be called when its device interrupts
 * ARGUMENTS:
 *        InterruptObject (OUT) = Points to the interrupt object created on 
 *                                return
 *        ServiceRoutine = Routine to be called when the device interrupts
 *        ServiceContext = Parameter to be passed to ServiceRoutine
 *        SpinLock = Initalized spinlock that will be used to synchronize
 *                   access between the isr and other driver routines. This is
 *                   required if the isr handles more than one vector or the
 *                   driver has more than one isr
 *        Vector = Interrupt vector to allocate 
 *                 (returned from HalGetInterruptVector)
 *        Irql = DIRQL returned from HalGetInterruptVector
 *        SynchronizeIrql = DIRQL at which the isr will execute. This must
 *                          be the highest of all the DIRQLs returned from
 *                          HalGetInterruptVector if the driver has multiple
 *                          isrs
 *        InterruptMode = Specifies if the interrupt is LevelSensitive or
 *                        Latched
 *        ShareVector = Specifies if the vector can be shared
 *        ProcessorEnableMask = Processors on the isr can run
 *        FloatingSave = TRUE if the floating point stack should be saved when
 *                       the isr runs. Must be false for x86 drivers
 * RETURNS: Status
 * IRQL: PASSIVE_LEVEL
 */
{
   KIRQL oldlvl;
   KIRQL synch_oldlvl;
   PKINTERRUPT ListHead;
   
   ASSERT_IRQL(PASSIVE_LEVEL);
   
   /*
    * Check the parameters
    */
   if (Vector >= NR_IRQS)
     {
	return(STATUS_INVALID_PARAMETER);
     }
   if (FloatingSave == TRUE)
     {
	return(STATUS_INVALID_PARAMETER);
     }
   
   /*
    * Acquire the table spinlock
    */
   KeAcquireSpinLock(&isr_table_lock,&oldlvl);
   
   /*
    * Check if the vector is already in use that we can share it
    */
   ListHead = CONTAINING_RECORD(isr_table[Vector].Flink,KINTERRUPT,Entry);
   if (!IsListEmpty(&isr_table[Vector]) &&
       (ShareVector == FALSE || ListHead->Shareable==FALSE))
     {
	KeReleaseSpinLock(&isr_table_lock,oldlvl);
	return(STATUS_INVALID_PARAMETER);
     }
   else
     {   
	isr_lock[Vector]=ExAllocatePool(NonPagedPool,sizeof(KSPIN_LOCK));
	KeInitializeSpinLock(isr_lock[Vector]);
	isr_lock[Vector]->irql = SynchronizeIrql;
     }
   
   /*
    * Initialize interrupt object
    */
   (*InterruptObject)=ExAllocatePool(NonPagedPool,sizeof(KINTERRUPT));
   if ((*InterruptObject)==NULL)
     {
	return(STATUS_INSUFFICIENT_RESOURCES);
     }
   (*InterruptObject)->ServiceContext = ServiceContext;
   (*InterruptObject)->ServiceRoutine = ServiceRoutine;
   (*InterruptObject)->Vector = Vector;
   (*InterruptObject)->ProcessorEnableMask = ProcessorEnableMask;
   (*InterruptObject)->SynchLevel = SynchronizeIrql;
   (*InterruptObject)->Shareable = ShareVector;
   (*InterruptObject)->FloatingSave = FALSE;
   (*InterruptObject)->IrqLock = isr_lock[Vector];
   
   KeRaiseIrql((*InterruptObject)->SynchLevel,&synch_oldlvl);
   KeAcquireSpinLockAtDpcLevel((*InterruptObject)->IrqLock);
   DPRINT("%x %x\n",isr_table[Vector].Flink,isr_table[Vector].Blink);
   InsertTailList(&isr_table[Vector],&((*InterruptObject)->Entry));
   DPRINT("%x %x\n",(*InterruptObject)->Entry.Flink,
          (*InterruptObject)->Entry.Blink);
   KeReleaseSpinLockFromDpcLevel((*InterruptObject)->IrqLock);
   KeLowerIrql(synch_oldlvl);
   
   /*
    * Release the table spinlock
    */
   KeReleaseSpinLock(&isr_table_lock,oldlvl);
   
   return(STATUS_SUCCESS);
}


VOID IoDisconnectInterrupt(PKINTERRUPT InterruptObject)
/*
 * FUNCTION: Releases a drivers isr
 * ARGUMENTS:
 *        InterruptObject = isr to release
 */
{
   KIRQL oldlvl;
   
   KeRaiseIrql(InterruptObject->SynchLevel,&oldlvl);
   KeAcquireSpinLockAtDpcLevel(InterruptObject->IrqLock);
   RemoveEntryFromList(&isr_table[InterruptObject->Vector],
		       &InterruptObject->Entry);
   KeReleaseSpinLockFromDpcLevel(InterruptObject->IrqLock);
   KeLowerIrql(oldlvl);
}

ULONG HalGetInterruptVector(INTERFACE_TYPE InterfaceType,
			    ULONG BusNumber,
			    ULONG BusInterruptLevel,
			    ULONG BusInterruptVector,
			    PKIRQL Irql,
			    PKAFFINITY Affinity)
/*
 * FUNCTION: Returns a mapped system interrupt vector for passing to
 * IoConnectInterrupt
 * ARGUMENTS:
 *          InterfaceType = Type bus the device is on
 *          BusNumber = Zero based number of the bus
 *          BusInterruptLevel = Bus specific interrupt level
 *          BusInterruptVector = Bus specific interrupt vector
 *          Irql (IN/OUT) = On entry the bus relative IRQL
 *                          On exit the DIRQL
 *          Affinity (OUT) = Caller supplied storage for the interrupt 
 *                           affinity mask
 * RETURNS: The mapped vector
 */
{
   ULONG ret;
   
//   ASSERT_IRQL(PASSIVE_LEVEL);
   
   switch (InterfaceType)
     {
      case Internal:
	*Irql = HIGH_LEVEL -  BusInterruptVector;
	ret = BusInterruptVector;
	break;
	
      default:
	ret = -1;
	printk("(%s:%d) Don't know that bus type\n",__FILE__,__LINE__);
	break;
     }
   return(ret);
}


