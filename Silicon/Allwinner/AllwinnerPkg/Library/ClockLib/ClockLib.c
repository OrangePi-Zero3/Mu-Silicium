#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/ClockLib.h>

#include "ClockDefinitions.h"

STATIC
CONST CCM_REG_BIT *
CcmLookup (
  IN CONST CCM_REG_BIT  *Table,
  IN UINTN               Count,
  IN UINT32              Id
  )
{
  if ((Id >= Count) || (Table[Id].Offset == 0)) {
    DEBUG ((DEBUG_ERROR, "CCM: no table entry for ID %u\n", Id));
    ASSERT (FALSE);
    return NULL;
  }

  return &Table[Id];
}

#define GATE_OF(Id)   CcmLookup (mGates,  ARRAY_SIZE (mGates),  (Id))
#define RESET_OF(Id)  CcmLookup (mResets, ARRAY_SIZE (mResets), (Id))

VOID
CcmClkEnable (
  IN UINT32  ClkId
  )
{
  CONST CCM_REG_BIT  *G = GATE_OF (ClkId);

  if (G != NULL) {
    MmioOr32 (H618_CCM_BASE + G->Offset, G->Mask);
  }
}

VOID
CcmClkDisable (
  IN UINT32  ClkId
  )
{
  CONST CCM_REG_BIT  *G = GATE_OF (ClkId);

  if (G != NULL) {
    MmioAnd32 (H618_CCM_BASE + G->Offset, ~G->Mask);
  }
}

VOID
CcmClkConfigure (
  IN UINT32  ClkId,
  IN UINT32  Mux,
  IN UINT32  DivM,
  IN UINT32  DivN
  )
{
  CONST CCM_REG_BIT  *G = GATE_OF (ClkId);
  UINTN              Reg;

  ASSERT (Mux  <= CCM_CLK_MUX_MASK);
  ASSERT (DivM <= CCM_CLK_DIV_M_MASK);
  ASSERT (DivN <= CCM_CLK_DIV_N_MASK);

  if (G == NULL) {
    return;
  }

  Reg = H618_CCM_BASE + G->Offset;

  MmioAnd32 (Reg, ~G->Mask);                      // gate off while retuning

  MmioAndThenOr32 (
    Reg,
    ~(UINT32)((CCM_CLK_MUX_MASK   << CCM_CLK_MUX_SHIFT)   |
              (CCM_CLK_DIV_M_MASK << CCM_CLK_DIV_M_SHIFT) |
              (CCM_CLK_DIV_N_MASK << CCM_CLK_DIV_N_SHIFT)),
    (Mux  << CCM_CLK_MUX_SHIFT)   |
    (DivM << CCM_CLK_DIV_M_SHIFT) |
    (DivN << CCM_CLK_DIV_N_SHIFT)
    );

  MicroSecondDelay (10);
  MmioOr32 (Reg, G->Mask);
  MicroSecondDelay (10);
}

VOID
CcmResetAssert (
  IN UINT32  RstId
  )
{
  CONST CCM_REG_BIT  *R = RESET_OF (RstId);

  if (R != NULL) {
    MmioAnd32 (H618_CCM_BASE + R->Offset, ~R->Mask);   // low = in reset
  }
}

VOID
CcmResetDeassert (
  IN UINT32  RstId
  )
{
  CONST CCM_REG_BIT  *R = RESET_OF (RstId);

  if (R != NULL) {
    MmioOr32 (H618_CCM_BASE + R->Offset, R->Mask);
  }
}

VOID
CcmResetPulse (
  IN UINT32  RstId
  )
{
  CcmResetAssert (RstId);
  MicroSecondDelay (20);
  CcmResetDeassert (RstId);
  MicroSecondDelay (20);
}

VOID
CcmBusEnable (
  IN UINT32  ClkId,
  IN UINT32  RstId
  )
{
  CcmResetAssert (RstId);
  CcmClkDisable (ClkId);
  MicroSecondDelay (10);
  CcmClkEnable (ClkId);
  CcmResetDeassert (RstId);
  MicroSecondDelay (10);
}

VOID
CcmBusUngate (
  IN UINT32  ClkId,
  IN UINT32  RstId
  )
{
  CcmClkEnable (ClkId);
  CcmResetDeassert (RstId);
}

BOOLEAN
CcmPllEnableAndLock (
  IN UINTN  Reg
  )
{
  UINTN  Timeout;

  MmioOr32 (Reg, CCM_PLL_LDO_EN_BIT | CCM_PLL_ENABLE_BIT);
  MmioOr32 (Reg, CCM_PLL_LOCK_ENABLE_BIT);

  for (Timeout = 5000; Timeout != 0; Timeout--) {
    if ((MmioRead32 (Reg) & CCM_PLL_LOCK_STATUS_BIT) != 0) {
      MicroSecondDelay (20);
      MmioOr32 (Reg, CCM_PLL_OUT_EN_BIT);   // ungate output only once locked
      return TRUE;
    }
    MicroSecondDelay (1);
  }

  DEBUG ((DEBUG_ERROR, "CCM: PLL at 0x%08x failed to lock, val=0x%08x\n",
    (UINT32)Reg, MmioRead32 (Reg)));
  return FALSE;
}