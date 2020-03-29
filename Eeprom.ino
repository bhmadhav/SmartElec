/*
   EEPROM Access

   Stores values non-volatile data into the EEPROM.

*/

#include <EEPROM.h>
#include "SmartElec.h"

Ticker nvram_commit_ticker;
#define NvramCommitInterruptVal    5   // ticker value in seconds
static boolean commit_pending = false;

void nvram_commit_ticker_callback()
{
  if (commit_pending == true)
  {
    smart_elec_commit_nvram();
    commit_pending = false;
  }
}

// the current address in the EEPROM (i.e. which byte
// we're going to write to next)
int addr = 0;

void smart_elec_init_nvram(void) 
{
  EEPROM.begin(sizeof(SmartElecNvram));
  // Initialize and Enable the NVRAM commit ticker
  nvram_commit_ticker.attach(NvramCommitInterruptVal, nvram_commit_ticker_callback);
}

void smart_elec_read_nvram(struct SmartElecNvram *val) 
{
  char *tmp = (char *)val;
  for (int i = 0; i < sizeof(SmartElecNvram); i++)
    tmp[i] = EEPROM.read(i);
}

void smart_elec_write_nvram(struct SmartElecNvram *val) 
{
  char *tmp = (char *)val;
  for (int i = 0; i < sizeof(SmartElecNvram); i++)
    EEPROM.write(i, tmp[i]);
  // set the commit pending flag
  commit_pending = true;
}

void smart_elec_commit_nvram() 
{
  EEPROM.commit();
}
