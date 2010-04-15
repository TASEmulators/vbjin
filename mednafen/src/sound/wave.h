#ifndef _MDFN_WAVE_H
#define _MDFN_WAVE_H

void MDFN_WriteWaveData(int16 *Buffer, int Count);
int MDFNI_EndWaveRecord(void);

bool MDFN_WaveRecordActive(void);

#endif
