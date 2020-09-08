#ifndef DELAYTIMER_h
#define DELAYTIMER_h

#include "Arduino.h"

class DelayTimer {

public:
	// ----- Constructor -----
	DelayTimer(uint32_t msDelay = 0, uint32_t msNow = 0);

	void setDelay(uint32_t msDelay);
	void reset(uint32_t msNow, uint32_t msDelay = 0);
	bool tripped(uint32_t msNow = 0);

private:
	uint32_t _msDelay;                                                         // Delay in ms
	uint32_t _msLast;                                                          // When delay timer started

};

#endif

// End
