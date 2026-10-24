#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <Arduino.h>

bool IrRemotePrepare(void);  // fixed persistent IR code tables; heap-neutral
void IrRemoteBegin(void);
void IrRemoteStop(void);
void IrRemoteProcess(void);

// Light-sleep integration for explicit GPIO12=IR mode. The normal edge ISR is
// stopped before GPIO12 is temporarily changed to a LOW-level wake source, then
// restored after wake. If GPIO12 is still LOW on wake, the remaining part of
// the wake pulse is seeded as the first MARK of a new edge-captured frame.
void IrRemoteResumeAfterLightSleep(bool seedActiveLowPulse);
// After an IR-caused GPIO wake, consume edge-decoded frames without dispatching
// normal runtime actions. Returns true only when the learned STANDBY command is
// decoded before timeout; all other IR commands are rejected by the sleep gate.
bool IrRemoteQualifyStandbyWake(uint32_t timeoutMs);

void IrRemoteUiOpen(void);
void IrRemoteUiRotate(int8_t direction);
bool IrRemoteUiPress(void);   // true = return to main Settings menu
void IrRemoteUiAbort(void);
bool IrRemoteUiActive(void);
bool IrRemoteHasProfile(void);

#endif
