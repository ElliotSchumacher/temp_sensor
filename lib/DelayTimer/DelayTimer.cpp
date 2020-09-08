#include "DelayTimer.h"

DelayTimer::DelayTimer(uint32_t msDelay, uint32_t msNow) {
	_msDelay = msDelay;
	_msLast = msNow ? msNow : millis();
} // DelayTimer()

void DelayTimer::setDelay(uint32_t msDelay) {
	_msDelay = msDelay;
} // setDelay()

void DelayTimer::reset(uint32_t msNow, uint32_t msDelay) {
	if(msDelay) _msDelay = msDelay;
	_msLast = msNow ? msNow : millis();
} // reset()

bool DelayTimer::tripped(uint32_t msNow) {
	if(!msNow) msNow = millis();
	if(msNow - _msLast >= _msDelay) {
        _msLast = msNow;
		return true;
	} else {
    	return false;
	}
} // tripped()

// End
