#pragma once

// Compartilha a configuração do SDK para manter o layout das classes idêntico.
// A cópia antiga definia LINUX=0 no aplicativo e LINUX=1 no runtime, causando
// alocações menores que XHostThread e corrupção de memória.
#include_next <rex/platform.h>

#ifndef REX_PLATFORM_HORIZON
#define REX_PLATFORM_HORIZON REX_PLATFORM_SWITCH
#endif
