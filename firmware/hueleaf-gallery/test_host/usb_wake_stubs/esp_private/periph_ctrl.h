#pragma once
#define PERIPH_RCC_ATOMIC() for (int atomic_once=1; atomic_once; atomic_once=0)
