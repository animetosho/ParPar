Log Table Tricks
===========================================

A number of GF operations can be (or ideally are) done via a log and exponentiation table. For GF(2<sup>16</sup>), unfortunately the table can take up some space (128KB each), but I’ve found a few tricks that can be applied to reduce the size of these, which is useful for reducing cache footprint at the expense of more operations being performed.

## Exponent Table

Computing an exponent can be broken up into parts:

Given n = x+y, 2<sup>n</sup> = 2<sup>x</sup> × 2<sup>y</sup>

We can select *y* such that multiplying by 2<sup>y</sup> is easy to do. For example, where 0≤*y*≤7, the operation can be done by shifting-left by *y* bits, then reducing back into the field via a lookup. This allows the 16-bit *n* to be broken up into a 13-bit high part, with a 3-bit low part, since the low part falls in the 0-7 range.

The exponentiation can be computed by regular lookup for the high part of the number (i.e. compute 2<sup>x</sup>). Because we know the lower bits of this computation are always 0, this allows the lookup table to skip over those entries and thus be smaller. For a 13-bit number in GF(2<sup>16</sup>), this is a 16KB table.

Next, the ‘multiply by the low part’ (i.e. multiply by 2<sup>y</sup>) can be computed by shifting the number left by *y* bits, then using a lookup table to reduce the product back into the field. For a 3-bit low part, the maximum number of bits that can be shifted out of the field is 7, so we need a 2<sup>7</sup>=128 entry table. In GF(2<sup>16</sup>), this table would be 256 bytes.

> **Example**:  evaluate 2<sup>12345</sup>
> Bottom 3 bits of 12345 is 001 (12345 & 7 = 1)
> Thus, breaking 12345 into a 13-bit + 3-bit parts results in 12344 + 1
> We compute 2<sup>12344</sup> using a compact log table (can use 12344\>\>3 = 1543 as index), then multiply the result by 2<sup>1</sup> (shift-left by 1, then reducing the result back into the field).

This trick allows us to reduce a GF(2<sup>16</sup>) exponent table from 128KB to 16.25KB, at the cost of more shifting and an extra lookup.

```c
// standard solution (128KB table)
// compute exp table
uint16_t exp_table[65536];
for(uint16_t n=0; n<65536; n++)
    exp_table[n] = gf16_power(2, n);

// use exp table
uint16_t gf16_exp(uint16_t n) {
    return exp_table[n];
}
```

```c
// compact table solution (16KB + 0.25KB table)
// compute exp/reduction table
uint16_t exp_table[8192];
uint16_t reduction_table[128];
for(uint16_t n=0; n<8192; n++)
    exp_table[n] = gf16_power(256, n); // or gf16_power(2, n*8)
for(uint16_t n=0; n<128; n++) {
    int r = n << 9; // align n to top 7 bits of 16-bit r
    // perform 7x multiply-by-2 operations
    for (int i=0; i<7; i++) {
        r <<= 1;
        if(r & 0x10000) r ^= 0x1100B;
    }
    reduction_table[n] = r;
}

// use tables to compute exp
uint16_t gf16_exp(uint16_t n) {
    // lookup the high part (compute 2**x)
    uint32_t result = exp_table[n >> 3];
    // multiply by low part (result *= 2**y)
    result <<= n & 7;
    // reduce the result back into the field
    return result ^ reduction_table[result >> 16];
}
```

The table can be further reduced by splitting the operation further, but this only provides greatly diminishing returns.

## Log Table

Reducing the size of the log table is more difficult. This may be due to the [discrete logarithm](https://en.wikipedia.org/wiki/Discrete_logarithm) problem making it difficult to split the operation up into smaller operations.

However, there are some things that can be done:

The log table size can be halved if only every odd entry is stored, with a bit scan to perform the first log iteration:

```c
uint16_t gf16_log(uint16_t n) { // assumes n != 0
    int log = __builtin_ctz(n); // or _BitScanForward for MSVC
    n >>= log; // lowest bit of n is now guaranteed to be set
    log += log_table[n | 1]; // for an actual half table implementation, use log_table[n >> 1] instead
    return log;
}
```

For GF(2<sup>16</sup>) with polynomial 0x1100b, the table can be further reduced to 38~40KB, albeit with a fair amount of extra work:

```c
typedef struct { uint16_t reduction; uint8_t shift; } log_prep_t;
log_prep_t log_prep[2048];
uint16_t small_log_table[16384];
{ // compute lookup tables
    const int POLYNOMIAL = 0x1100b;
    int n = 1;
    // store every 4th logarithm
    for(int i=0; i<65535; i++) {
    	if((n & 3) == 3) small_log_table[n >> 2] = i;
        n <<= 1;
        if(n > 0xffff) n ^= POLYNOMIAL;
	}
    // generate lookup table for initial iterative approach
    for(int i=0; i<2048; i++) {
        log_prep_t prep = {0, 0};
        int n = i;
        while((n&3) != 3 && prep.shift < 10) {
            if(n & 1) {
                n ^= POLYNOMIAL;
                prep.reduction ^= POLYNOMIAL;
            }
            n >>= 1;
            prep.reduction >>= 1;
            prep.shift++;
        }
        log_prep[i] = prep;
    }
}

uint16_t gf16_log(uint16_t n) {
    // perform iterative log calculation for up to 20 iterations
    int log = 0;
    for(int i=0; i<2; i++) {
        // scan bottom 11 bits for how much of the log we can compute
        log_prep_t prep = log_prep[n & 0x7ff];
        log += prep.shift;
        n >>= prep.shift;
        n ^= prep.reduction;
    }
    // the bottom two bits of n are now guaranteed to be set
    log += small_log_table[n >> 2];
    if(log >= 0xffff)
        log -= 0xffff;
    return log;
}
```

The way this technique works is to iteratively perform the log operation until the bottom two bits are set. It just so happens that this can be achieved with a maximum of 20 iterations for any 16-bit value with polynomial 0x1100b. This 20-iteration scan can be achieved by breaking it into two 10-iteration lookup steps, which requires a 2<sup>11</sup> entry table.

Once we have the bottom two bits being a known value, a smaller log lookup table can be used to compute the final logarithm.

## PAR2 Input Coefficients

Although this isn’t a log/exponent table, PAR2’s input coefficients, which are often written to a table, can have its size reduced as well.

The [PAR2 spec](http://parchive.sourceforge.net/docs/specifications/parity-volume-spec/article-spec.html#i__134603784_890) requires coefficients to be (2<sup>n</sup>)<sup>r</sup>, where *n* refers to the input slice constant, and *r* refers to the recovery slice number.

A number of PAR2 implementations compute 2<sup>n</sup> and store this in a table, which consumes up to 64KB (if you're not using all 32768 available input slices, the table can be shrunk to the number of slices used). After finding the appropriate 2<sup>n</sup> value, it's exponentiated by *r*, via a logarithm + multiply + exponentiation method. The logarithm and exponentiation operations each require a 128KB lookup table (ignoring the tricks shown above).

```c
// standard approach
uint16_t input_coeff[32768];
int i = 0;
for(int n=0; n<32768; n++) {
    if(!(n%3) && !(n%5) && !(n%17) && !(n%257))
        input_coeff[i++] = exp_table[n];
}

uint16_t get_coefficient(uint16_t input_index, uint16_t recovery_index) {
    int n = input_coeff[input_index];
    // compute gf16_power(n, recovery_index)
    n = log_table[n];
    n = (n * recovery_index) % 65535;
    return exp_table[n];
}
```

However, instead of storing 2<sup>n</sup>, we can just store *n*. This eliminates the need to perform a logarithm operation, since (2<sup>n</sup>)<sup>r</sup> = 2<sup>nr</sup> .

```c
// no log_table approach
uint16_t input_log[32768];
int i = 0;
for(int n=0; n<32768; n++) {
    if(!(n%3) && !(n%5) && !(n%17) && !(n%257))
        input_log[i++] = n;
}

uint16_t get_coefficient(uint16_t input_index, uint16_t recovery_index) {
    int n = input_log[input_index];
    n = (n * recovery_index) % 65535;
    return exp_table[n];
}
```

Although I don't know an efficient way to get the input slice constant (*n*) from an input slice number (*i*), it turns out that `2*i` is a close approximation of *n* (-4 ≤ `n - i*2` ≤ 5). We can use this trick to store `n - 2*i` and reconstruct *n* using this difference. Because the difference is much smaller, it can be stored in one byte (or even 4 bits), meaning that the input constant lookup table can be reduced from 64KB to 32KB.
In theory, the constant can probably be determined iteratively without the need for a lookup table at all, but I feel that it makes things complicated, and since the table is mostly used in a sequential fashion, it should be relatively efficient, despite its size.

```c
// compact table approach
uint8_t input_log_diff[32768];
int i = 0;
for(int n=0; n<32768; n++) {
    if(!(n%3) && !(n%5) && !(n%17) && !(n%257))
        input_log_diff[i++] = n - i*2;
}

uint16_t get_coefficient(uint16_t input_index, uint16_t recovery_index) {
    int n = input_index*2 + input_log_diff[input_index];
    n = (n * recovery_index) % 65535;
    return exp_table[n];
}
```

