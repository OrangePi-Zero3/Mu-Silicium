#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/Rng.h>

#define H616_SID_BASE  0x03006000

STATIC UINT64 mRngState;

STATIC
UINT64
RngNext (
  VOID
  )
{
  mRngState ^= mRngState << 13;
  mRngState ^= mRngState >> 7;
  mRngState ^= mRngState << 17;
  return mRngState;
}

STATIC EFI_STATUS EFIAPI
RngGetInfo (
  IN EFI_RNG_PROTOCOL    *This,
  IN OUT UINTN            *RNGAlgorithmListSize,
  OUT EFI_RNG_ALGORITHM  *RNGAlgorithmList OPTIONAL
  )
{
  if (RNGAlgorithmListSize == NULL)
    return EFI_INVALID_PARAMETER;

  if (*RNGAlgorithmListSize < sizeof (EFI_RNG_ALGORITHM)) {
    *RNGAlgorithmListSize = sizeof (EFI_RNG_ALGORITHM);
    return EFI_BUFFER_TOO_SMALL;
  }
  
  if (RNGAlgorithmList == NULL)
    return EFI_INVALID_PARAMETER;

  CopyGuid (RNGAlgorithmList, &gEfiRngAlgorithmRaw);
  *RNGAlgorithmListSize = sizeof (EFI_RNG_ALGORITHM);

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
RngGetRng (
  IN EFI_RNG_PROTOCOL   *This,
  IN EFI_RNG_ALGORITHM  *RNGAlgorithm OPTIONAL,
  IN UINTN               RNGValueLength,
  OUT UINT8             *RNGValue
  )
{
  UINTN  Index;
  UINT64 Value;

  if (RNGValue == NULL || RNGValueLength == 0)
    return EFI_INVALID_PARAMETER;

  if (RNGAlgorithm != NULL && !CompareGuid (RNGAlgorithm, &gEfiRngAlgorithmRaw))
    return EFI_UNSUPPORTED;


  for (Index = 0; Index < RNGValueLength; Index++) {
    if ((Index % sizeof (Value)) == 0)
      Value = RngNext ();

    RNGValue[Index] = (UINT8)(Value >> ((Index % sizeof (Value)) * 8));
  }
  return EFI_SUCCESS;
}

STATIC EFI_RNG_PROTOCOL mRngProtocol = {
  RngGetInfo,
  RngGetRng
};

EFI_STATUS EFIAPI
RngDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;
  UINT64      Counter;

  // This is the furthest from cryptographically secure that I have ever seen in my life.
  Counter = GetPerformanceCounter ();
  mRngState = Counter ^ ((UINT64)MmioRead32 (H616_SID_BASE) << 32) ^ MmioRead32 (H616_SID_BASE + 0x0C) ^ (UINTN)&mRngState;

  if (mRngState == 0)
    mRngState = 0x9E3779B97F4A7C15;

  Handle = NULL;
  return gBS->InstallMultipleProtocolInterfaces (&Handle, &gEfiRngProtocolGuid, &mRngProtocol, NULL);
}
