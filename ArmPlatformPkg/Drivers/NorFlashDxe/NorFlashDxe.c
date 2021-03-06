/** @file  NorFlashDxe.c

  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PcdLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Guid/GlobalVariable.h>
#include <Guid/EventGroup.h>

#include "NorFlashDxe.h"
#ifdef APM_XGENE_GFC_FLASH
#include <ArmPlatform.h>
#include <Library/TimerLib.h>
#endif

STATIC EFI_EVENT  mNorFlashVirtualAddrChangeEvent = NULL;

//
// Global variable declarations
//
NOR_FLASH_INSTANCE **mNorFlashInstances;
UINTN                *mNorFlashDeviceCount;
/* This buffer used for runtime */
VOID                 *TmpRuntimeBuffer;

NOR_FLASH_INSTANCE  mNorFlashInstanceTemplate = {
  NOR_FLASH_SIGNATURE, // Signature
  NULL, // Handle ... NEED TO BE FILLED

  FALSE, // Initialized
  NULL, // Initialize

  0, // DeviceBaseAddress ... NEED TO BE FILLED
  0, // RegionBaseAddress ... NEED TO BE FILLED
  0, // Size ... NEED TO BE FILLED
  0, // StartLba

  {
    EFI_BLOCK_IO_PROTOCOL_REVISION2, // Revision
    NULL, // Media ... NEED TO BE FILLED
    NorFlashBlockIoReset, // Reset;
    NorFlashBlockIoReadBlocks,          // ReadBlocks
    NorFlashBlockIoWriteBlocks,         // WriteBlocks
    NorFlashBlockIoFlushBlocks          // FlushBlocks
  }, // BlockIoProtocol

  {
    0, // MediaId ... NEED TO BE FILLED
    FALSE, // RemovableMedia
    TRUE, // MediaPresent
    FALSE, // LogicalPartition
    FALSE, // ReadOnly
    FALSE, // WriteCaching;
    0, // BlockSize ... NEED TO BE FILLED
    4, //  IoAlign
    0, // LastBlock ... NEED TO BE FILLED
    0, // LowestAlignedLba
    1, // LogicalBlocksPerPhysicalBlock
  }, //Media;

  FALSE, // SupportFvb ... NEED TO BE FILLED
  {
    FvbGetAttributes, // GetAttributes
    FvbSetAttributes, // SetAttributes
    FvbGetPhysicalAddress,  // GetPhysicalAddress
    FvbGetBlockSize,  // GetBlockSize
    FvbRead,  // Read
    FvbWrite, // Write
    FvbEraseBlocks, // EraseBlocks
    NULL, //ParentHandle
    FvbGetMappedAddress,
  }, //  FvbProtoccol;

  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        (UINT8)( sizeof(VENDOR_DEVICE_PATH)      ),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
      },
      { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }, // GUID ... NEED TO BE FILLED
    },
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      sizeof (EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  } // DevicePath
};

#ifdef APM_XGENE_GFC_FLASH

struct NorCmdSequence NorCmdSet2CmdSequence[] = {

/* READ Commands  */
  /* CMDSET2_CMD_READ_DEVICE_ID */ {
	{0x555, 0x2AA, 0x555, 0x001, 0x00E, 0x00F},
	{0xAA, 0x55, 0x90, 0x227E, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_READ_STATUS_REGISTER */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_CLEAR_STATUS_REGISTER */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_READ_ARRAY */ {
	{0x555, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_READ_CFI_QUERY */ {
	{0x555, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0x98, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

/* WRITE Commands */
  /* CMDSET2_CMD_WORD_PROGRAM_SETUP */ {
	{0x555, 0x2AA, 0x555, 0xFFF, 0xFFF, 0xFFF},
	{0xAA, 0x55, 0xA0, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_ALTERNATE_WORD_PROGRAM_SETUP */ {
	{0x555, 0x2AA, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xAA, 0x55, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_BUFFERED_PROGRAM_SETUP */ {
	{0x555, 0x2AA, 0xEEE, 0xFFF, 0xFFF, 0xFFF},
	{0xAA, 0x55, 0x25, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_BUFFERED_PROGRAM_CONFIRM */ {
	{0xEEE, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0x29, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_BEFP_SETUP */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_BEFP_CONFIRM */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

/* ERASE Commands */
  /* CMDSET2_CMD_BLOCK_ERASE_SETUP */ {
	{0x555, 0x2AA, 0x555, 0xFFF, 0xFFF, 0xFFF},
	{0xAA, 0x55, 0x80, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_BLOCK_ERASE_CONFIRM */ {
	{0x555, 0x2AA, 0xEEE, 0xFFF, 0xFFF, 0xFFF},
	{0xAA, 0x55, 0x30, 0xFF, 0xFF, 0xFF}
	},

/* SUSPEND Commands */
  /* CMDSET2_CMD_PROGRAM_OR_ERASE_SUSPEND */ {
	{0xEEE, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xB0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_SUSPEND_RESUME */ {
	{0xEEE, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

/* BLOCK LOCKING / UNLOCKING Commands */
  /* CMDSET2_CMD_LOCK_BLOCK_SETUP */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_LOCK_BLOCK */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_UNLOCK_BLOCK */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_LOCK_DOWN_BLOCK */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

/* PROTECTION Commands */
  /* CMDSET2_CMD_PROGRAM_PROTECTION_REGISTER_SETUP */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

/* CONFIGURATION Commands  */
  /* CMDSET2_CMD_READ_CONFIGURATION_REGISTER_SETUP */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},
  /* CMDSET2_CMD_READ_CONFIGURATION_REGISTER */ {
	{0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	},

};

EFI_PHYSICAL_ADDRESS XgeneNor0Base = APM_XGENE_NOR0_BASE;

VOID NorCmdSet2SendNorCmd(UINTN BaseAddr, UINT32 Offset, enum NorFlashCmd Cmd)
{
	INT32 i = 0;
	UINTN Addr = 0;

	for (i = 0 ; i < MaxNorCmdSet2Cycles ; i++) {
		if (NorCmdSet2CmdSequence[Cmd].NorBusCycleAddr[i] == 0xFFF)
			break;
		if (NorCmdSet2CmdSequence[Cmd].NorBusCycleAddr[i] == 0xEEE) {
		  Addr = CREATE_NOR_ADDRESS(BaseAddr, Offset);
		}
		else {
			Addr = CREATE_NOR_ADDRESS(XgeneNor0Base, NorCmdSet2CmdSequence[Cmd].NorBusCycleAddr[i]);
		}
		//DEBUG((EFI_D_WARN, "Addr=%p Data=%x\n", Addr, NorCmdSet2CmdSequence[Cmd].NorBusCycleData[i]));
		// ASSERT(1);
		//MmioWrite16(Addr,NorCmdSet2CmdSequence[Cmd].NorBusCycleData[i]);
		*((volatile UINT16*)Addr) = NorCmdSet2CmdSequence[Cmd].NorBusCycleData[i];
		asm volatile("dsb sy":::);
		asm volatile("isb":::);
		//MicroSecondDelay(10);
	}
	//DEBUG((EFI_D_WARN, "\n"));
}

UINTN NorSectorAddr( IN UINTN Offset)
{
	UINTN NorSectAddr = 0ULL;
	UINTN B1 = 4ULL * SIZE_32KB;
	UINTN B2 = B1 + 254ULL * SIZE_128KB;
	if (Offset < B1) {
		NorSectAddr = (Offset - Offset % SIZE_32KB);
	} else if (Offset < B2) {
		NorSectAddr = (Offset - Offset % SIZE_128KB);
	}  else {
		NorSectAddr = (Offset - Offset % SIZE_32KB);
	}
	return NorSectAddr;
}

UINTN NorNextSectorAddr( IN UINTN Offset)
{
  UINTN NorNextSectAddr = 0ULL;
  UINTN B1 = 4ULL * SIZE_32KB;
  UINTN B2 = B1 + 254ULL * SIZE_128KB;
  UINTN B3 = B2 + B1;
  if (Offset < B1) {
    NorNextSectAddr = Offset + SIZE_32KB - Offset % SIZE_32KB;
  } else if (Offset < B2) {
    NorNextSectAddr = Offset + SIZE_128KB - Offset % SIZE_128KB;
    NorNextSectAddr = ((NorNextSectAddr >= B3) ? (B2 + SIZE_32KB) : NorNextSectAddr);
  } else {
    NorNextSectAddr = Offset + SIZE_32KB - Offset % SIZE_32KB;
  }
  return NorNextSectAddr;
}

EFI_STATUS
NorFlashEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  );

EFI_STATUS
NorFlashEraseSectors (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  Address,
  IN UINTN                  Size
  );
#endif

EFI_STATUS
NorFlashCreateInstance (
  IN UINTN                  NorFlashDeviceBase,
  IN UINTN                  NorFlashRegionBase,
  IN UINTN                  NorFlashSize,
  IN UINT32                 MediaId,
  IN UINT32                 BlockSize,
  IN BOOLEAN                SupportFvb,
  IN CONST GUID             *NorFlashGuid,
  OUT NOR_FLASH_INSTANCE**  NorFlashInstance
  )
{
  EFI_STATUS Status;
  NOR_FLASH_INSTANCE* Instance;

  ASSERT(NorFlashInstance != NULL);

  Instance = AllocateRuntimeCopyPool (sizeof(NOR_FLASH_INSTANCE),&mNorFlashInstanceTemplate);
  if (Instance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG((DEBUG_BLKIO, "FvbGetMappedAddress: 0x%p\n", FvbGetMappedAddress));

  Instance->DeviceBaseAddress = NorFlashDeviceBase;
  Instance->RegionBaseAddress = NorFlashRegionBase;
  Instance->Size = NorFlashSize;

  Instance->BlockIoProtocol.Media = &Instance->Media;
  Instance->Media.MediaId = MediaId;
  Instance->Media.BlockSize = BlockSize;
  Instance->Media.LastBlock = (NorFlashSize / BlockSize)-1;

  CopyGuid (&Instance->DevicePath.Vendor.Guid,NorFlashGuid);

  if (SupportFvb) {
    Instance->SupportFvb = TRUE;
    Instance->Initialize = NorFlashFvbInitialize;

    Status = gBS->InstallMultipleProtocolInterfaces (
                  &Instance->Handle,
                  &gEfiDevicePathProtocolGuid, &Instance->DevicePath,
                  &gEfiBlockIoProtocolGuid,  &Instance->BlockIoProtocol,
                  &gEfiFirmwareVolumeBlockProtocolGuid, &Instance->FvbProtocol,
                  NULL
                  );
    if (EFI_ERROR(Status)) {
      FreePool(Instance);
      return Status;
    }
  } else {
    Instance->Initialized = TRUE;

    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Instance->Handle,
                    &gEfiDevicePathProtocolGuid, &Instance->DevicePath,
                    &gEfiBlockIoProtocolGuid,  &Instance->BlockIoProtocol,
                    NULL
                    );
    if (EFI_ERROR(Status)) {
      FreePool(Instance);
      return Status;
    }
  }

  *NorFlashInstance = Instance;
  return Status;
}

UINT32
NorFlashReadStatusRegister (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  SR_Address
  )
{
  // Prepare to read the status register
  #if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_STATUS_REGISTER);
  #elif defined(APM_XGENE_GFC_FLASH)
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_STATUS_REGISTER);
  #endif

  return MmioRead32 (Instance->DeviceBaseAddress);
}


BOOLEAN
NorFlashBlockIsLocked (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  BOOLEAN               BlockIsLocked;

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
  UINT32                LockStatus;

  BlockIsLocked = TRUE;

  // Send command for reading device id
  SEND_NOR_COMMAND (BlockAddress, 2, P30_CMD_READ_DEVICE_ID);

  // Read block lock status
  LockStatus = MmioRead32 (CREATE_NOR_ADDRESS(BlockAddress, 2));

  // Decode block lock status
  LockStatus = FOLD_32BIT_INTO_16BIT(LockStatus);

  if ((LockStatus & 0x2) != 0) {
    DEBUG((EFI_D_ERROR, "NorFlashBlockIsLocked: WARNING: Block LOCKED DOWN\n"));
  }

  if ((LockStatus & 0x1) == 0) {
    // This means the block is unlocked
    DEBUG((DEBUG_BLKIO, "UnlockSingleBlock: Block 0x%08x unlocked\n", BlockAddress));
    BlockIsLocked = FALSE;
  }
#else
    BlockIsLocked = FALSE;
#endif

  return BlockIsLocked;
}


EFI_STATUS
NorFlashUnlockSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  EFI_STATUS            Status = EFI_SUCCESS;

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)

  UINT32                LockStatus;

  // Raise the Task Priority Level to TPL_NOTIFY to serialise all its operations
  // and to protect shared data structures.

  if (FeaturePcdGet (PcdNorFlashCheckBlockLocked) == TRUE) {
    do {
      // Request a lock setup
      SEND_NOR_COMMAND (BlockAddress, 0, P30_CMD_LOCK_BLOCK_SETUP);

      // Request an unlock
      SEND_NOR_COMMAND (BlockAddress, 0, P30_CMD_UNLOCK_BLOCK);

      // Send command for reading device id
      SEND_NOR_COMMAND (BlockAddress, 2, P30_CMD_READ_DEVICE_ID);

      // Read block lock status
      LockStatus = MmioRead32 (CREATE_NOR_ADDRESS(BlockAddress, 2));

      // Decode block lock status
      LockStatus = FOLD_32BIT_INTO_16BIT(LockStatus);
    } while ((LockStatus & 0x1) == 1);
  } else {
    // Request a lock setup
    SEND_NOR_COMMAND (BlockAddress, 0, P30_CMD_LOCK_BLOCK_SETUP);

    // Request an unlock
    SEND_NOR_COMMAND (BlockAddress, 0, P30_CMD_UNLOCK_BLOCK);

    // Wait until the status register gives us the all clear
    do {
      LockStatus = NorFlashReadStatusRegister (Instance, BlockAddress);
    } while ((LockStatus & P30_SR_BIT_WRITE) != P30_SR_BIT_WRITE);
  }

  // Put device back into Read Array mode
  SEND_NOR_COMMAND (BlockAddress, 0, P30_CMD_READ_ARRAY);
#endif

  DEBUG((DEBUG_BLKIO, "UnlockSingleBlock: BlockAddress=0x%08x, Exit Status = \"%r\".\n", BlockAddress, Status));

  return Status;
}


EFI_STATUS
NorFlashUnlockSingleBlockIfNecessary (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  EFI_STATUS Status = EFI_SUCCESS;

  if (NorFlashBlockIsLocked (Instance, BlockAddress) == TRUE) {
    Status = NorFlashUnlockSingleBlock (Instance, BlockAddress);
  }

  return Status;
}

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)

/**
 * The following function presumes that the block has already been unlocked.
 **/
EFI_STATUS
NorFlashEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  EFI_STATUS            Status;
  UINT32                StatusRegister;

  Status = EFI_SUCCESS;

  // Request a block erase and then confirm it
  SEND_NOR_COMMAND(BlockAddress, 0, P30_CMD_BLOCK_ERASE_SETUP);
  SEND_NOR_COMMAND(BlockAddress, 0, P30_CMD_BLOCK_ERASE_CONFIRM);

  // Wait until the status register gives us the all clear
  do {
    StatusRegister = NorFlashReadStatusRegister (Instance, BlockAddress);
  } while ((StatusRegister & P30_SR_BIT_WRITE) != P30_SR_BIT_WRITE);

  if (StatusRegister & P30_SR_BIT_VPP) {
    DEBUG((EFI_D_ERROR,"EraseSingleBlock(BlockAddress=0x%08x: VPP Range Error\n", BlockAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if ((StatusRegister & (P30_SR_BIT_ERASE | P30_SR_BIT_PROGRAM)) == (P30_SR_BIT_ERASE | P30_SR_BIT_PROGRAM)) {
    DEBUG((EFI_D_ERROR,"EraseSingleBlock(BlockAddress=0x%08x: Command Sequence Error\n", BlockAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_ERASE) {
    DEBUG((EFI_D_ERROR,"EraseSingleBlock(BlockAddress=0x%08x: Block Erase Error StatusRegister:0x%X\n", BlockAddress, StatusRegister));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_BLOCK_LOCKED) {
    // The debug level message has been reduced because a device lock might happen. In this case we just retry it ...
    DEBUG((EFI_D_INFO,"EraseSingleBlock(BlockAddress=0x%08x: Block Locked Error\n", BlockAddress));
    Status = EFI_WRITE_PROTECTED;
  }

  if (EFI_ERROR(Status)) {
    // Clear the Status Register
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_CLEAR_STATUS_REGISTER);
  }

  // Put device back into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_ARRAY);

  return Status;
}

#elif defined(APM_XGENE_GFC_FLASH)
EFI_STATUS
NorFlashEraseSingleSector (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  Sector
  )
{
  EFI_STATUS            Status;

  Status = EFI_SUCCESS;
  DEBUG((DEBUG_BLKIO,"NorFlashEraseSingleSector(Sector=0x%08x\n", Sector));
  // Request a block erase and then confirm it
  SEND_NOR_COMMAND(Instance->DeviceBaseAddress + NorSectorAddr(Sector), 0, CMDSET2_CMD_BLOCK_ERASE_SETUP);
  SEND_NOR_COMMAND(Instance->DeviceBaseAddress + NorSectorAddr(Sector), 0, CMDSET2_CMD_BLOCK_ERASE_CONFIRM);
  /* Sector Erase time : 2 s for 16Kword and 3.5 s for 64Kword  */
	/* page 72, S29WS-N_00_I2 Nov 10, 2006 */
  /* FIXME: Calculate time based on sector size */
#if !defined(APM_XGENE_NO_NORDELAY)
  MicroSecondDelay(1000*1000);
#endif
  // Put device back into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress + NorSectorAddr(Sector), 0, CMDSET2_CMD_READ_ARRAY);

  return Status;
}

EFI_STATUS
NorFlashEraseSectors (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  Sectors,
	IN UINTN                  Size
  )
{
  EFI_STATUS            Status;

  Status = EFI_SUCCESS;
  UINTN Sector;
  UINTN Start =  NorSectorAddr(Sectors);
  UINTN  End = NorNextSectorAddr(Sectors+Size - 1);
  for (Sector = Start ; Sector < End ; Sector = NorNextSectorAddr(Sector)) {
	NorFlashEraseSingleSector(Instance,Sector);
  }

  return Status;
}

EFI_STATUS
NorFlashEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  // Do nothing here
  // Erase is taken taken care by the write function
  // Keep UEFI happy
  return EFI_SUCCESS;
}
#else /* APM_XGENE_SPI_FLASH */

EFI_STATUS
NorFlashEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  DEBUG((DEBUG_BLKIO,"NorFlashEraseSingleBlock(Address=0x%08x\n", BlockAddress));
  return NorFlashPlatformErase(BlockAddress, Instance->Media.BlockSize);
}

#endif

/**
 * The following function presumes that the block has already been unlocked.
 **/
EFI_STATUS
NorFlashUnlockAndEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  )
{
  EFI_STATUS      Status;
  UINTN           Index;
  EFI_TPL         OriginalTPL;

  if (!EfiAtRuntime ()) {
    // Raise TPL to TPL_HIGH to stop anyone from interrupting us.
    OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  } else {
    // This initialization is only to prevent the compiler to complain about the
    // use of uninitialized variables
    OriginalTPL = TPL_HIGH_LEVEL;
  }

  Index = 0;
  // The block erase might fail a first time (SW bug ?). Retry it ...
  do {
    // Unlock the block if we have to
    Status = NorFlashUnlockSingleBlockIfNecessary (Instance, BlockAddress);
    if (!EFI_ERROR(Status)) {
      Status = NorFlashEraseSingleBlock (Instance, BlockAddress);
    }
    Index++;
  } while ((Index < NOR_FLASH_ERASE_RETRY) && (Status == EFI_WRITE_PROTECTED));

  if (Index == NOR_FLASH_ERASE_RETRY) {
    DEBUG((EFI_D_ERROR,"EraseSingleBlock(BlockAddress=0x%08x: Block Locked Error (try to erase %d times)\n", BlockAddress,Index));
  }

  if (!EfiAtRuntime()) {
    // Interruptions can resume.
    gBS->RestoreTPL (OriginalTPL);
  }

  return Status;
}

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
EFI_STATUS
NorFlashWriteSingleWord (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  WordAddress,
  IN UINT32                 WriteData
  )
{
  EFI_STATUS            Status;
  UINT32                StatusRegister;

  Status = EFI_SUCCESS;

  // Request a write single word command
  SEND_NOR_COMMAND(WordAddress, 0, P30_CMD_WORD_PROGRAM_SETUP);

  // Store the word into NOR Flash;
  MmioWrite32 (WordAddress, WriteData);

  // Wait for the write to complete and then check for any errors; i.e. check the Status Register
  do {
    // Prepare to read the status register
    StatusRegister = NorFlashReadStatusRegister (Instance, WordAddress);
    // The chip is busy while the WRITE bit is not asserted
  } while ((StatusRegister & P30_SR_BIT_WRITE) != P30_SR_BIT_WRITE);


  // Perform a full status check:
  // Mask the relevant bits of Status Register.
  // Everything should be zero, if not, we have a problem

  if (StatusRegister & P30_SR_BIT_VPP) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteSingleWord(WordAddress:0x%X): VPP Range Error\n",WordAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_PROGRAM) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteSingleWord(WordAddress:0x%X): Program Error\n",WordAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_BLOCK_LOCKED) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteSingleWord(WordAddress:0x%X): Device Protect Error\n",WordAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (!EFI_ERROR(Status)) {
    // Clear the Status Register
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_CLEAR_STATUS_REGISTER);
  }

  // Put device back into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_ARRAY);

  return Status;
}
#endif

/*
 * Writes data to the NOR Flash using the Buffered Programming method.
 *
 * The maximum size of the on-chip buffer is 32-words, because of hardware restrictions.
 * Therefore this function will only handle buffers up to 32 words or 128 bytes.
 * To deal with larger buffers, call this function again.
 *
 * This function presumes that both the TargetAddress and the TargetAddress+BufferSize
 * exist entirely within the NOR Flash. Therefore these conditions will not be checked here.
 *
 * In buffered programming, if the target address not at the beginning of a 32-bit word boundary,
 * then programming time is doubled and power consumption is increased.
 * Therefore, it is a requirement to align buffer writes to 32-bit word boundaries.
 * i.e. the last 4 bits of the target start address must be zero: 0x......00
 */
#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
EFI_STATUS
NorFlashWriteBuffer (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  TargetAddress,
  IN UINTN                  BufferSizeInBytes,
  IN UINT32                 *Buffer
  )
{
  EFI_STATUS            Status;
  UINTN                 BufferSizeInWords;
  UINTN                 Count;
  volatile UINT32       *Data;
  UINTN                 WaitForBuffer;
  BOOLEAN               BufferAvailable;
  UINT32                StatusRegister;

  WaitForBuffer   = MAX_BUFFERED_PROG_ITERATIONS;
  BufferAvailable = FALSE;

  // Check that the target address does not cross a 32-word boundary.
  if ((TargetAddress & BOUNDARY_OF_32_WORDS) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  // Check there are some data to program
  if (BufferSizeInBytes == 0) {
    return EFI_BUFFER_TOO_SMALL;
  }

  // Check that the buffer size does not exceed the maximum hardware buffer size on chip.
  if (BufferSizeInBytes > P30_MAX_BUFFER_SIZE_IN_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // Check that the buffer size is a multiple of 32-bit words
  if ((BufferSizeInBytes % 4) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // Pre-programming conditions checked, now start the algorithm.

  // Prepare the data destination address
  Data = (UINT32 *)TargetAddress;

  // Check the availability of the buffer
  do {
    // Issue the Buffered Program Setup command
    SEND_NOR_COMMAND(TargetAddress, 0, P30_CMD_BUFFERED_PROGRAM_SETUP);

    // Read back the status register bit#7 from the same address
    if (((*Data) & P30_SR_BIT_WRITE) == P30_SR_BIT_WRITE) {
      BufferAvailable = TRUE;
    }

    // Update the loop counter
    WaitForBuffer--;

  } while ((WaitForBuffer > 0) && (BufferAvailable == FALSE));

  // The buffer was not available for writing
  if (WaitForBuffer == 0) {
    Status = EFI_DEVICE_ERROR;
    goto EXIT;
  }

  // From now on we work in 32-bit words
  BufferSizeInWords = BufferSizeInBytes / (UINTN)4;

  // Write the word count, which is (buffer_size_in_words - 1),
  // because word count 0 means one word.
  SEND_NOR_COMMAND(TargetAddress, 0, (BufferSizeInWords - 1));

  // Write the data to the NOR Flash, advancing each address by 4 bytes
  for(Count=0; Count < BufferSizeInWords; Count++, Data++, Buffer++) {
    *Data = *Buffer;
  }

  // Issue the Buffered Program Confirm command, to start the programming operation
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_BUFFERED_PROGRAM_CONFIRM);

  // Wait for the write to complete and then check for any errors; i.e. check the Status Register
  do {
    StatusRegister = NorFlashReadStatusRegister (Instance, TargetAddress);
    // The chip is busy while the WRITE bit is not asserted
  } while ((StatusRegister & P30_SR_BIT_WRITE) != P30_SR_BIT_WRITE);


  // Perform a full status check:
  // Mask the relevant bits of Status Register.
  // Everything should be zero, if not, we have a problem

  Status          = EFI_SUCCESS;

  if (StatusRegister & P30_SR_BIT_VPP) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteBuffer(TargetAddress:0x%X): VPP Range Error\n", TargetAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_PROGRAM) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteBuffer(TargetAddress:0x%X): Program Error\n", TargetAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (StatusRegister & P30_SR_BIT_BLOCK_LOCKED) {
    DEBUG((EFI_D_ERROR,"NorFlashWriteBuffer(TargetAddress:0x%X): Device Protect Error\n",TargetAddress));
    Status = EFI_DEVICE_ERROR;
  }

  if (!EFI_ERROR(Status)) {
    // Clear the Status Register
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_CLEAR_STATUS_REGISTER);
  }

EXIT:
  // Put device back into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_ARRAY);

  return Status;
}
#elif defined(APM_XGENE_GFC_FLASH)

EFI_STATUS
NorFlashWriteBuffer (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  TargetAddress,
  IN UINTN                  BufferSizeInBytes,
  IN UINT32                 *Buffer
  )
{
  EFI_STATUS            Status;
  UINTN                 BufferSizeInWords;
  UINTN                 Count;
  volatile UINT16 *BufferPtr = (volatile UINT16 *)Buffer;
  volatile UINT16       *Data;

  // Pre-programming conditions checked, now start the algorithm.

  // Prepare the data destination address
  Data = (UINT16 *)(TargetAddress);
//  DEBUG((EFI_D_ERROR,"NorFlashWriteBuffer(TargetAddress:0x%X)\n",TargetAddress));
//  DEBUG((EFI_D_ERROR,"NorFlashWriteBuffer(BufferSizeInBytes:0x%X)\n",BufferSizeInBytes));
  SEND_NOR_COMMAND(Instance->DeviceBaseAddress + NorSectorAddr(TargetAddress - Instance->DeviceBaseAddress), 0, CMDSET2_CMD_BUFFERED_PROGRAM_SETUP);

  // From now on we work in 16-bit words
  BufferSizeInWords = BufferSizeInBytes / (UINTN)2;

  // Write the word count, which is (buffer_size_in_words - 1),
  // because word count 0 means one word.
  SEND_TO_NOR(Instance->DeviceBaseAddress + NorSectorAddr(TargetAddress - Instance->DeviceBaseAddress), 0, BufferSizeInWords-1);


  // Write the data to the NOR Flash, advancing each address by 4 bytes
  for(Count=0; Count < BufferSizeInWords; Count++, Data++, BufferPtr++) {
    SEND_TO_NOR((UINTN)Data, 0, *BufferPtr);
  }

  // Issue the Buffered Program Confirm command, to start the programming operation
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress + NorSectorAddr(TargetAddress - Instance->DeviceBaseAddress), 0, CMDSET2_CMD_BUFFERED_PROGRAM_CONFIRM);

#if !defined(APM_XGENE_NO_NORDELAY)
  MicroSecondDelay(1000);
#endif
  Status          = EFI_SUCCESS;

  // Put device back into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);

  return Status;
}
#endif

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
EFI_STATUS
NorFlashWriteSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINT32                 *DataBuffer,
  IN UINT32                 BlockSizeInWords
  )
{
  EFI_STATUS    Status;
  UINTN         WordAddress;
  UINT32        WordIndex;
  UINTN         BufferIndex;
  UINTN         BlockAddress;
  UINTN         BuffersInBlock;
  UINTN         RemainingWords;
  EFI_TPL       OriginalTPL;

  Status = EFI_SUCCESS;

  // Get the physical address of the block
  BlockAddress = GET_NOR_BLOCK_ADDRESS (Instance->RegionBaseAddress, Lba, BlockSizeInWords * 4);

  // Start writing from the first address at the start of the block
  WordAddress = BlockAddress;

  if (!EfiAtRuntime ()) {
    // Raise TPL to TPL_HIGH to stop anyone from interrupting us.
    OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  } else {
    // This initialization is only to prevent the compiler to complain about the
    // use of uninitialized variables
    OriginalTPL = TPL_HIGH_LEVEL;
  }

  Status = NorFlashUnlockAndEraseSingleBlock (Instance, BlockAddress);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "WriteSingleBlock: ERROR - Failed to Unlock and Erase the single block at 0x%X\n", BlockAddress));
    goto EXIT;
  }

  // To speed up the programming operation, NOR Flash is programmed using the Buffered Programming method.

  // Check that the address starts at a 32-word boundary, i.e. last 7 bits must be zero
  if ((WordAddress & BOUNDARY_OF_32_WORDS) == 0x00) {

    // First, break the entire block into buffer-sized chunks.
    BuffersInBlock = (UINTN)(BlockSizeInWords * 4) / P30_MAX_BUFFER_SIZE_IN_BYTES;

    // Then feed each buffer chunk to the NOR Flash
    for(BufferIndex=0;
         BufferIndex < BuffersInBlock;
         BufferIndex++, WordAddress += P30_MAX_BUFFER_SIZE_IN_BYTES, DataBuffer += P30_MAX_BUFFER_SIZE_IN_WORDS
      ) {
      Status = NorFlashWriteBuffer (Instance, WordAddress, P30_MAX_BUFFER_SIZE_IN_BYTES, DataBuffer);
      if (EFI_ERROR(Status)) {
        goto EXIT;
      }
    }

    // Finally, finish off any remaining words that are less than the maximum size of the buffer
    RemainingWords = BlockSizeInWords % P30_MAX_BUFFER_SIZE_IN_WORDS;

    if(RemainingWords != 0) {
      Status = NorFlashWriteBuffer (Instance, WordAddress, (RemainingWords * 4), DataBuffer);
      if (EFI_ERROR(Status)) {
        goto EXIT;
      }
    }

  } else {
    // For now, use the single word programming algorithm
    // It is unlikely that the NOR Flash will exist in an address which falls within a 32 word boundary range,
    // i.e. which ends in the range 0x......01 - 0x......7F.
    for(WordIndex=0; WordIndex<BlockSizeInWords; WordIndex++, DataBuffer++, WordAddress = WordAddress + 4) {
      Status = NorFlashWriteSingleWord (Instance, WordAddress, *DataBuffer);
      if (EFI_ERROR(Status)) {
        goto EXIT;
      }
    }
  }

EXIT:
  if (!EfiAtRuntime ()) {
    // Interruptions can resume.
    gBS->RestoreTPL (OriginalTPL);
  }

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "NOR FLASH Programming [WriteSingleBlock] failed at address 0x%08x. Exit Status = \"%r\".\n", WordAddress, Status));
  }
  return Status;
}

#endif

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)

EFI_STATUS
NorFlashWriteBlocks (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSizeInBytes,
  IN VOID                   *Buffer,
  IN BOOLEAN                Erase
  )
{
  UINT32          *pWriteBuffer;
  EFI_STATUS      Status = EFI_SUCCESS;
  EFI_LBA         CurrentBlock;
  UINT32          BlockSizeInWords;
  UINT32          NumBlocks;
  UINT32          BlockCount;

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if(Instance->Media.ReadOnly == TRUE) {
    return EFI_WRITE_PROTECTED;
  }

  // We must have some bytes to read
  DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: BufferSizeInBytes=0x%x\n", BufferSizeInBytes));
  if(BufferSizeInBytes == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // The size of the buffer must be a multiple of the block size
  DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: BlockSize in bytes =0x%x\n", Instance->Media.BlockSize));
  if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // All blocks must be within the device
  NumBlocks = ((UINT32)BufferSizeInBytes) / Instance->Media.BlockSize ;

  DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: NumBlocks=%d, LastBlock=%ld, Lba=%ld.\n", NumBlocks, Instance->Media.LastBlock, Lba));

  if ((Lba + NumBlocks) > (Instance->Media.LastBlock + 1)) {
    DEBUG((EFI_D_ERROR, "NorFlashWriteBlocks: ERROR - Write will exceed last block.\n"));
    return EFI_INVALID_PARAMETER;
  }

  BlockSizeInWords = Instance->Media.BlockSize / 4;

  // Because the target *Buffer is a pointer to VOID, we must put all the data into a pointer
  // to a proper data type, so use *ReadBuffer
  pWriteBuffer = (UINT32 *)Buffer;

  CurrentBlock = Lba;
  for (BlockCount=0; BlockCount < NumBlocks; BlockCount++, CurrentBlock++, pWriteBuffer = pWriteBuffer + BlockSizeInWords) {

    DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: Writing block #%d\n", (UINTN)CurrentBlock));

    Status = NorFlashWriteSingleBlock (Instance, CurrentBlock, pWriteBuffer, BlockSizeInWords);

    if (EFI_ERROR(Status)) {
      break;
    }
  }

  DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: Exit Status = \"%r\".\n", Status));
  return Status;
}

EFI_STATUS
NorFlashReadBlocks (
  IN NOR_FLASH_INSTANCE   *Instance,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSizeInBytes,
  OUT VOID                *Buffer
  )
{
  UINT32              NumBlocks;
  UINTN               StartAddress;

  DEBUG((DEBUG_BLKIO, "NorFlashReadBlocks: BufferSize=0x%xB BlockSize=0x%xB LastBlock=%ld, Lba=%ld.\n",
      BufferSizeInBytes, Instance->Media.BlockSize, Instance->Media.LastBlock, Lba));

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Return if we have not any byte to read 
  if (BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  // The size of the buffer must be a multiple of the block size
  if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // All blocks must be within the device
  NumBlocks = ((UINT32)BufferSizeInBytes) / Instance->Media.BlockSize ;

  if ((Lba + NumBlocks) > (Instance->Media.LastBlock + 1)) {
    DEBUG((EFI_D_ERROR, "NorFlashReadBlocks: ERROR - Read will exceed last block\n"));
    return EFI_INVALID_PARAMETER;
  }

  // Get the address to start reading from
  StartAddress = GET_NOR_BLOCK_ADDRESS (Instance->RegionBaseAddress,
                                        Lba,
                                        Instance->Media.BlockSize
                                       );

  // Put the device into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_ARRAY);

  // Readout the data
  CopyMem(Buffer, (UINTN *)StartAddress, BufferSizeInBytes);

  return EFI_SUCCESS;
}
#elif defined(APM_XGENE_GFC_FLASH)

EFI_STATUS
NorFlashWriteBlocks (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSizeInBytes,
  IN VOID                   *Buffer,
  IN BOOLEAN                Erase
  )
{
  UINTN      X1, X2, X3;
  UINTN      S1, S2, S3;
  UINTN      O1, O2, O3;
  UINTN      base_offset;
  EFI_LBA    Lba1, Lba2, Lba3;
  UINT8     *Buffer1 = NULL, *Buffer2 =  NULL, *Buffer3 = NULL;
  EFI_TPL    OriginalTPL;

  // if not initialized, initialize instance first
  if (!Instance->Initialized && Instance->Initialize) {
    Instance->Initialize(Instance);
  }

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Instance->Media.ReadOnly == TRUE) {
    return EFI_WRITE_PROTECTED;
  }

  // We must have some bytes to read
  // DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: BufferSizeInBytes=0x%x\n", BufferSizeInBytes));
  if (BufferSizeInBytes == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // Determine the Buffer limits
  base_offset = Instance->RegionBaseAddress - Instance->DeviceBaseAddress;
  O2 = Lba * Instance->Media.BlockSize + base_offset;
  O1 = NorSectorAddr(O2);
  X1 = Instance->DeviceBaseAddress + O1;
  S1 = O2 - O1;
  X2 = Instance->DeviceBaseAddress + O2;
  S2 = BufferSizeInBytes;
  O3 = O2 + BufferSizeInBytes;
  X3 = Instance->DeviceBaseAddress + O3;
  if (NorSectorAddr(O3) == O3)
    S3 = 0;
  else
    S3 = NorNextSectorAddr(O3) - O3;

  Lba1 = (X1-Instance->DeviceBaseAddress-base_offset)/Instance->Media.BlockSize;
  Lba2 = (X2-Instance->DeviceBaseAddress-base_offset)/Instance->Media.BlockSize;
  Lba3 = (X3-Instance->DeviceBaseAddress-base_offset)/Instance->Media.BlockSize;
  DEBUG((DEBUG_BLKIO, "Lba=%ld Lbal=%ld Lba2=%ld Lba3=%ld\n",
	Lba, Lba1, Lba2, Lba3));
  DEBUG((DEBUG_BLKIO, "O1=0x%lx O2=0x%lx O3=0x%lx X1=0x%lx X2=0x%lx X3=0x%lx "
	"S1=0x%lx S2=0x%lx S3=0x%lx\n", O1, O2, O3, X1, X2, X3, S1, S2, S3)); ASSERT(1);

  if (!VALID_NOR_ADDR(X1, Instance->DeviceBaseAddress)) {
    DEBUG((DEBUG_BLKIO, "Invalid address: 0x%lx\n", X1));
    ASSERT(1);
    return EFI_INVALID_PARAMETER;
  }

  if (!VALID_NOR_ADDR(X2, Instance->DeviceBaseAddress)) {
    DEBUG((DEBUG_BLKIO, "Invalid address: 0x%lx\n", X2));
    ASSERT(1);
    return EFI_INVALID_PARAMETER;
  }

  if (!VALID_NOR_ADDR(X3, Instance->DeviceBaseAddress)) {
    DEBUG((DEBUG_BLKIO, "Invalid address: 0x%lx\n", X3));
    ASSERT(1);
    return EFI_INVALID_PARAMETER;
  }

  // Entire sector is erase in an erase operation
  // Preserve the data that is not in the range
  if (S1) {
    Buffer1 = TmpRuntimeBuffer;
    ASSERT(S1 < MAX_RUNTIME_BUFFER_SIZE);
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    NorFlashReadBlocks(Instance,Lba1,S1,Buffer1);
    DEBUG((DEBUG_BLKIO, "SDD: Buffer1 1 0x%lx\n", Buffer1)); ASSERT(1);
  }
  Buffer2 = (UINT8 *) Buffer;
  if (S3) {
    Buffer3 = TmpRuntimeBuffer;
    ASSERT(S3 < MAX_RUNTIME_BUFFER_SIZE);
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    NorFlashReadBlocks(Instance,Lba3,S3,Buffer3);
    DEBUG((DEBUG_BLKIO, "SDD: 1\n")); ASSERT(1);
  }

  // Raise TPL to TPL_HIGH to stop anyone from interrupting us.
  OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);

  // Erase and write
  if (S1) {
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    NorFlashEraseSectors(Instance, X1 - Instance->DeviceBaseAddress, S1);
  }
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
  NorFlashEraseSectors(Instance, X2 - Instance->DeviceBaseAddress, S2);
  if (S3) {
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    NorFlashEraseSectors(Instance, X3 - Instance->DeviceBaseAddress, S3);
  }
  if (S1) {
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    DEBUG((DEBUG_BLKIO, "SDD: 2\n")); ASSERT(1);
    NorFlashWriteMultipleBuffers(Instance, X1, S1, Buffer1);
  }
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
  NorFlashWriteMultipleBuffers(Instance, X2, S2, Buffer2);
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
  if (S3) {
    NorFlashWriteMultipleBuffers(Instance, X3, S3, Buffer3);
    SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
    DEBUG((DEBUG_BLKIO, "SDD: 3\n")); ASSERT(1);
  }

  // Interruptions can resume.
  gBS->RestoreTPL (OriginalTPL);

  return EFI_SUCCESS;
}

EFI_STATUS
NorFlashWriteMultipleBuffers (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  Addr,
  IN UINTN                  BufferSizeInBytes,
  IN UINT8                  *Buffer
  )
{
  UINT8           *pWriteBuffer;
  EFI_STATUS      Status = EFI_SUCCESS;
  UINT32          NumBytesWritten;
  EFI_LBA         DstPtr;
  EFI_LBA         Next32KBoundary;
  UINTN           Incr;

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if(Instance->Media.ReadOnly == TRUE) {
    return EFI_WRITE_PROTECTED;
  }

  // If no bytes, return success
  // DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: BufferSizeInBytes=0x%x\n", BufferSizeInBytes));
  if(BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  // Ensure alignment
  if ((Addr % 2) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  // Write in chunks of CMDSET2_MAX_BUFFER_SIZE_IN_BYTES,
  // breaking at 32K boundaries
  pWriteBuffer = (UINT8 *)Buffer;
  for (DstPtr = Addr, NumBytesWritten = 0; NumBytesWritten < BufferSizeInBytes; ) {
    Next32KBoundary = ((DstPtr / SIZE_32KB) + 1) * SIZE_32KB;
    Incr = ((DstPtr + CMDSET2_MAX_BUFFER_SIZE_IN_BYTES <  Next32KBoundary) ? CMDSET2_MAX_BUFFER_SIZE_IN_BYTES : (Next32KBoundary - DstPtr));

    Status = NorFlashWriteBuffer (Instance, DstPtr, Incr, (UINT32 *)pWriteBuffer);
    if (EFI_ERROR(Status)) {
      break;
    }
    DstPtr += Incr;
    pWriteBuffer += Incr;
    NumBytesWritten += Incr;

  }

  // DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: Exit Status = \"%r\".\n", Status));
  return Status;
}

EFI_STATUS
NorFlashReadBlocks (
  IN NOR_FLASH_INSTANCE   *Instance,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSizeInBytes,
  OUT VOID                *Buffer
  )
{
  UINTN i;
  UINTN Addr = Lba * Instance->Media.BlockSize + Instance->RegionBaseAddress;

  if (!VALID_NOR_ADDR(Addr, Instance->DeviceBaseAddress)) {
    DEBUG((DEBUG_BLKIO, "Invalid address: 0x%lx\n", Addr));
    ASSERT(1);
    return EFI_INVALID_PARAMETER;
  }

  volatile UINT16 *SrcPtr = (volatile UINT16*) Addr;
  volatile UINT16 *DstPtr = (volatile UINT16*) Buffer;
  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // We must have some bytes to read
  DEBUG((DEBUG_BLKIO, "NorFlashReadBlocks: BufferSize=0x%x bytes.\n", BufferSizeInBytes));
  if (BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  // Put the device into Read Array mode
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);

  // Readout the data
  //CopyMem(Buffer, (UINTN *)Addr, BufferSizeInBytes);
  for (i = 0 ; i < BufferSizeInBytes ; i += 2, SrcPtr++, DstPtr++)  {
    // DEBUG((DEBUG_BLKIO, "SrcPtr=0x%lx DstPtr=0x%lx\n", SrcPtr, DstPtr)); ASSERT(TRUE);
    *DstPtr = *SrcPtr;
  }

  return EFI_SUCCESS;
}

#else /* APM_XGENE_SPI_FLASH */

EFI_STATUS
NorFlashWriteBlocks (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSizeInBytes,
  IN VOID                   *Buffer,
  IN BOOLEAN                Erase
  )
{
  EFI_STATUS      Status = EFI_SUCCESS;
  EFI_TPL         OriginalTPL = TPL_HIGH_LEVEL;
  UINT32 Addr = Lba * Instance->Media.BlockSize + Instance->RegionBaseAddress;

  // if not initialized, initialize instance first
  if (!Instance->Initialized && Instance->Initialize) {
    Instance->Initialize(Instance);
  }

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Instance->Media.ReadOnly == TRUE) {
    return EFI_WRITE_PROTECTED;
  }

  // We must have some bytes to read
  DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: BufferSizeInBytes=0x%x Buffer:0x%p\n", BufferSizeInBytes, Buffer));
  if (BufferSizeInBytes == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (!EfiAtRuntime ()) {
    // Raise TPL to TPL_HIGH to stop anyone from interrupting us.
    OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  }

  if (Erase) {
    Status = NorFlashPlatformErase(Addr, BufferSizeInBytes);
  }

  if (!EFI_ERROR(Status)) {
    Status = NorFlashPlatformWrite(Addr, (VOID *)Buffer, BufferSizeInBytes);
    DEBUG((DEBUG_BLKIO, "NorFlashWriteBlocks: Exit BufferSizeInBytes=0x%x Buffer:0x%p\n", BufferSizeInBytes, Buffer));
  }

  if (!EfiAtRuntime()) {
    // Interruptions can resume.
    gBS->RestoreTPL (OriginalTPL);
  }

  return Status;
}

EFI_STATUS NorFlashReadBlocks (
  IN NOR_FLASH_INSTANCE   *Instance,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSizeInBytes,
  OUT VOID                *Buffer
  )
{
  UINT32 Addr = Lba * Instance->Media.BlockSize + Instance->RegionBaseAddress;

  // if not initialized, initialize instance first
  if (!Instance->Initialized && Instance->Initialize) {
    Instance->Initialize(Instance);
  }

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // We must have some bytes to read
  DEBUG((DEBUG_BLKIO, "NorFlashReadBlocks: BufferSize=0x%x bytes.\n", BufferSizeInBytes));
  if (BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  return NorFlashPlatformRead(Addr, (VOID *)Buffer, BufferSizeInBytes);
}
#endif


EFI_STATUS
NorFlashReset (
  IN  NOR_FLASH_INSTANCE *Instance
  )
{
  // As there is no specific RESET to perform, ensure that the devices is in the default Read Array mode
  #if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, P30_CMD_READ_ARRAY);
  #elif defined(APM_XGENE_GFC_FLASH)
  SEND_NOR_COMMAND (Instance->DeviceBaseAddress, 0, CMDSET2_CMD_READ_ARRAY);
  #endif

  return EFI_SUCCESS;
}

/**
  Notification function of EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE.

  This is a notification function registered on EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE event.
  It convers pointer to new virtual address.

  @param  Event        Event whose notification function is being invoked.
  @param  Context      Pointer to the notification function's context.

**/
VOID
EFIAPI
NorFlashVirtualNotifyEvent (
  IN EFI_EVENT                            Event,
  IN VOID                                 *Context
  )
{
  INTN Index;
  for (Index = 0; Index < *mNorFlashDeviceCount; Index++) {
    if (!mNorFlashInstances[Index]->PhysicalAddress) {
      /* FIXME: EfiConvertPointer doesn't deal with zero address */
      mNorFlashInstances[Index]->PhysicalAddress |= 0x1;
    }
    if (!mNorFlashInstances[Index]->DeviceBaseAddress) {
      /* FIXME: EfiConvertPointer doesn't deal with zero address */
      mNorFlashInstances[Index]->DeviceBaseAddress |= 0x1;
    }
    if (!mNorFlashInstances[Index]->RegionBaseAddress) {
      /* FIXME: EfiConvertPointer doesn't deal with zero address */
      mNorFlashInstances[Index]->RegionBaseAddress |= 0x1;
    }
    EfiConvertPointer (0x0, (VOID **) &mNorFlashInstances[Index]->PhysicalAddress);
    EfiConvertPointer (0x0, (VOID **) &mNorFlashInstances[Index]->DeviceBaseAddress);
    EfiConvertPointer (0x0, (VOID **) &mNorFlashInstances[Index]->RegionBaseAddress);

    mNorFlashInstances[Index]->PhysicalAddress &= ~(EFI_PHYSICAL_ADDRESS)0x1;
    mNorFlashInstances[Index]->DeviceBaseAddress &= ~(EFI_PHYSICAL_ADDRESS)0x1;
    mNorFlashInstances[Index]->RegionBaseAddress &= ~(EFI_PHYSICAL_ADDRESS)0x1;
#if 0
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->DeviceBaseAddress);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->RegionBaseAddress);
#endif

    // Convert BlockIo protocol
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->BlockIoProtocol.FlushBlocks);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->BlockIoProtocol.ReadBlocks);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->BlockIoProtocol.Reset);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->BlockIoProtocol.WriteBlocks);

    // Convert Fvb
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.EraseBlocks);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.GetAttributes);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.GetBlockSize);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.GetPhysicalAddress);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.Read);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.SetAttributes);
    EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbProtocol.Write);

#if 0
    if (mNorFlashInstances[Index]->FvbBuffer != NULL) {
      EfiConvertPointer (0x0, (VOID**)&mNorFlashInstances[Index]->FvbBuffer);
     }
#endif
  }

  EfiConvertPointer (0x0, (VOID **) &mNorFlashInstances);
  EfiConvertPointer (0x0, (VOID **) &mNorFlashDeviceCount);
  EfiConvertPointer (0x0, (VOID **) &TmpRuntimeBuffer);
#ifdef APM_XGENE_GFC_FLASH
  EfiConvertPointer (0x0, (VOID **) &NorCmdSet2CmdSequence);
  if (!XgeneNor0Base) {
    XgeneNor0Base |= 0x1;
  }
  EfiConvertPointer (0x0, (VOID **) &XgeneNor0Base);
  XgeneNor0Base &= ~(EFI_PHYSICAL_ADDRESS)0x1;
#endif
}

EFI_STATUS
EFIAPI
NorFlashInitialise (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS              Status;
  UINT32                  Index;
  NOR_FLASH_DESCRIPTION*  NorFlashDevices;
  UINT32                  NorFlashDeviceCount;
  BOOLEAN                 ContainVariableStorage;

  Status = NorFlashPlatformInitialization ();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR,"NorFlashInitialise: Fail to initialize Nor Flash devices\n"));
    return Status;
  }

  Status = NorFlashPlatformGetDevices (&NorFlashDevices,&NorFlashDeviceCount);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR,"NorFlashInitialise: Fail to get Nor Flash devices\n"));
    return Status;
  }

  mNorFlashInstances = AllocateRuntimePool (sizeof(NOR_FLASH_INSTANCE*) * NorFlashDeviceCount);
  mNorFlashDeviceCount = AllocateRuntimePool (sizeof(UINTN));
  TmpRuntimeBuffer = AllocateRuntimePool (MAX_RUNTIME_BUFFER_SIZE);
  if (!mNorFlashInstances || !mNorFlashDeviceCount || !TmpRuntimeBuffer) {
    DEBUG((EFI_D_ERROR,"NorFlashInitialise: Out of resource\n"));
    return Status;
  }
  *mNorFlashDeviceCount = NorFlashDeviceCount;

  for (Index = 0; Index < NorFlashDeviceCount; Index++) {
    // Check if this NOR Flash device contain the variable storage region
	#ifdef APM_XGENE_GFC_FLASH
      DEBUG((EFI_D_WARN, "SDD:  NorFlashDevices[Index].DeviceBaseAddress =%p NorFlashDevices[Index].RegionBaseAddress = %p NorFlashDevices[Index].Size = %p\n", NorFlashDevices[Index].DeviceBaseAddress, NorFlashDevices[Index].RegionBaseAddress, NorFlashDevices[Index].Size));
      ASSERT(1);
	#endif

    ContainVariableStorage =
        (NorFlashDevices[Index].RegionBaseAddress <= PcdGet32 (PcdFlashNvStorageVariableBase)) &&
        (PcdGet32 (PcdFlashNvStorageVariableBase) + PcdGet32 (PcdFlashNvStorageVariableSize) <= NorFlashDevices[Index].RegionBaseAddress + NorFlashDevices[Index].Size);

    Status = NorFlashCreateInstance (
      NorFlashDevices[Index].DeviceBaseAddress,
      NorFlashDevices[Index].RegionBaseAddress,
      NorFlashDevices[Index].Size,
      Index,
      NorFlashDevices[Index].BlockSize,
      ContainVariableStorage,
      &NorFlashDevices[Index].Guid,
      &mNorFlashInstances[Index]
    );
    if (EFI_ERROR(Status)) {
      DEBUG((EFI_D_ERROR,"NorFlashInitialise: Fail to create instance for NorFlash[%d]\n",Index));
    }
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  NorFlashVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mNorFlashVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
