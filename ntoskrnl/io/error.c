/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            kernel/base/bug.c
 * PURPOSE:         Graceful system shutdown if a bug is detected
 * PROGRAMMER:      David Welch (welch@mcmail.com)
 * UPDATE HISTORY:
 *                  Created 22/05/98
 */

/* INCLUDES *****************************************************************/

#include <internal/kernel.h>
#include <internal/linkage.h>
#include <ddk/ntddk.h>

#include <internal/debug.h>

/* FUNCTIONS *****************************************************************/

BOOLEAN IoIsErrorUserInduced(NTSTATUS Status)
{
   switch(Status)
     {
      case STATUS_DEVICE_NOT_READY:
      case STATUS_IO_TIMEOUT:
      case STATUS_MEDIA_WRITE_PROTECTED:
      case STATUS_NO_MEDIA_IN_DRIVE:
      case STATUS_VERIFY_REQUIRED:
      case STATUS_UNRECOGNIZED_MEDIA:
      case STATUS_WRONG_VOLUME:
	return(TRUE);
     }
   return(FALSE);
}

VOID IoSetHardErrorOrVerifyDevice(PIRP Irp, PDEVICE_OBJECT DeviceObject)
{
   UNIMPLEMENTED;
}

VOID IoRaiseHardError(PIRP Irp, PVPB Vpb, PDEVICE_OBJECT RealDeviceObject)
{
   UNIMPLEMENTED;
}

BOOLEAN IoIsTotalDeviceFailure(NTSTATUS Status)
{
   UNIMPLEMENTED;
}

BOOLEAN IoRaiseInformationalHardError(NTSTATUS ErrorStatus,
				      PUNICODE_STRING String,
				      PKTHREAD Thread)
{
   UNIMPLEMENTED;
}
