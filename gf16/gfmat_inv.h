#ifndef GFMAT_INV_H
#define GFMAT_INV_H

#include <vector>
#include "../src/stdint.h"

#ifdef PARPAR_INVERT_SUPPORT
uint16_t* compute_recovery_matrix(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, unsigned& stride);
void free_recovery_matrix(uint16_t* mat);
#endif

#endif
