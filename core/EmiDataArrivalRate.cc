//
//  EmiDataArrivalRate.cc
//  rock
//
//  Created by Per Eckerdal on 2012-05-22.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include "EmiDataArrivalRate.h"

EmiDataArrivalRate::EmiDataArrivalRate() :
_lastPacket(-1),
_medianFilter(1) {}

EmiDataArrivalRate::~EmiDataArrivalRate() {}