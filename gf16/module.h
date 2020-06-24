#include "../src/stdint.h"

void ppgf_init_gf_module();
void ppgf_init_constants();

void ppgf_omp_check_num_threads();
void ppgf_multiply_mat(const void* const* inputs, uint_fast16_t* iNums, unsigned int numInputs, size_t len, void** outputs, uint_fast16_t* oNums, unsigned int numOutputs, int add);

void ppgf_prep_input(size_t destLen, size_t inputLen, char* dest, char* src);
void ppgf_finish_input(unsigned int numInputs, uint16_t** inputs, size_t len);
void ppgf_get_method(int* rMethod, const char** rMethLong, int* align, int* stride);
int ppgf_set_method(int meth, int size_hint);

void ppgf_maybe_setup_gf();
int ppgf_get_num_threads();
void ppgf_set_num_threads(int threads);
