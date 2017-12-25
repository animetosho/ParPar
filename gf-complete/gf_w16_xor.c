
/* type returned by *movemask* function */
#if MWORD_SIZE == 64
# define umask_t uint64_t
/* fix PACKUS not crossing lanes + reverse order for mask extraction */
/* why isn't the reverse pattern: 2,0,6,4,3,1,7,5 ? */
# define PERMUTE_FIX_REV(v) _mm512_permutexvar_epi64(_mm512_set_epi64(6,4,2,0,7,5,3,1), v)
#elif MWORD_SIZE == 32
# define umask_t uint32_t
# define PERMUTE_FIX_REV(v) _mm256_permute4x64_epi64(v, 0x8D) /* 2,0,3,1 */
#else
# define umask_t uint16_t
# define PERMUTE_FIX_REV(v) (v)
#endif
#if MWORD_SIZE == 64
# define MOVMASK _mm512_movepi8_mask
#else
# define MOVMASK _MM(movemask_epi8)
#endif

static void _FN(gf_w16_xor_start)(void* src, int bytes, void* dest) {
	gf_region_data rd;
	_mword *sW;
	umask_t *d16, *top16;
	umask_t dtmp[128];
	_mword ta, tb, lmask, th, tl;
	int i, j;
	
	lmask = _MM(set1_epi16)(0xff);
	
	if(((uintptr_t)src & (MWORD_SIZE-1)) != ((uintptr_t)dest & (MWORD_SIZE-1))) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, MWORD_SIZE, MWORD_SIZE*16);
		
		memcpy(rd.d_top, (void*)((uintptr_t)src + (uintptr_t)rd.d_top - (uintptr_t)rd.dest), (uintptr_t)rd.dest + rd.bytes - (uintptr_t)rd.d_top);
		memcpy(rd.dest, src, (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		
		sW = (_mword*)((uintptr_t)src + (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		d16 = (umask_t*)rd.d_start;
		top16 = (umask_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _MMI(loadu)( sW);
				tb = _MMI(loadu)(sW+1);
				
				/* split to high/low parts */
				th = _MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				);
				tl = _MM(packus_epi16)(
					_MMI(and)(tb, lmask),
					_MMI(and)(ta, lmask)
				);
				tl = PERMUTE_FIX_REV(tl);
				th = PERMUTE_FIX_REV(th);
				
				/* save to dest by extracting 16-bit masks */
				dtmp[0+j] = MOVMASK(th);
				for(i=1; i<8; i++) {
					th = _MM(slli_epi16)(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = MOVMASK(th);
				}
				dtmp[64+j] = MOVMASK(tl);
				for(i=1; i<8; i++) {
					tl = _MM(slli_epi16)(tl, 1);
					dtmp[64+i*8+j] = MOVMASK(tl);
				}
				sW += 2;
			}
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128; /*==15*8*/
		}
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, MWORD_SIZE, MWORD_SIZE*16);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		sW = (_mword*)rd.s_start;
		d16 = (umask_t*)rd.d_start;
		top16 = (umask_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _MMI(load)( sW);
				tb = _MMI(load)(sW+1);
				
				/* split to high/low parts */
				th = _MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				);
				tl = _MM(packus_epi16)(
					_MMI(and)(tb, lmask),
					_MMI(and)(ta, lmask)
				);
				tl = PERMUTE_FIX_REV(tl);
				th = PERMUTE_FIX_REV(th);
				
				/* save to dest by extracting masks */
				dtmp[0+j] = MOVMASK(th);
				for(i=1; i<8; i++) {
					th = _MM(slli_epi16)(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = MOVMASK(th);
				}
				dtmp[64+j] = MOVMASK(tl);
				for(i=1; i<8; i++) {
					tl = _MM(slli_epi16)(tl, 1);
					dtmp[64+i*8+j] = MOVMASK(tl);
				}
				sW += 2;
			}
			/* we only really need to copy temp -> dest if src==dest */
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128;
		}
	}
	_MM_END
}

static void _FN(gf_w16_xor_final)(void* src, int bytes, void* dest) {
	gf_region_data rd;
	umask_t *s16, *d16, *top16;
	_mword ta, tb, lmask, th, tl;
	ALIGN(MWORD_SIZE, umask_t dtmp[128]);
	int i, j;
	
	/*shut up compiler warning*/
	th = _MMI(setzero)();
	tl = _MMI(setzero)();
	
	if(((uintptr_t)src & (MWORD_SIZE-1)) != ((uintptr_t)dest & (MWORD_SIZE-1))) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, MWORD_SIZE, MWORD_SIZE*16);
		
		memcpy((void*)((uintptr_t)dest + (uintptr_t)rd.s_top - (uintptr_t)rd.src), rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
		memcpy(dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		
		d16 = (umask_t*)((uintptr_t)dest + (uintptr_t)rd.s_start - (uintptr_t)rd.src);
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, MWORD_SIZE, MWORD_SIZE*16);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		d16 = (umask_t*)rd.d_start;
	}
	
	lmask = _MM(set1_epi16)(0xff);
	s16 = (umask_t*)rd.s_start;
	top16 = (umask_t*)rd.s_top;
	while(s16 != top16) {
		for(j=0; j<8; j++) {
			/* load in pattern: [0011223344556677] [8899AABBCCDDEEFF] */
#if MWORD_SIZE == 64
			tl = _MM(i32gather_epi64)(_mm256_set_epi32(64, 72, 80, 88, 96, 104, 112, 120), s16, 8);
			th = _MM(i32gather_epi64)(_mm256_set_epi32(0, 8, 16, 24, 32, 40, 48, 56), s16, 8);
			
# define _P(a,b) (((a)<<16)|(b))
# define _Q(n) _P(28+n,24+n), _P(20+n,16+n), _P(12+n,8+n), _P(4+n,0+n)
			tl = _mm512_permutexvar_epi16(_mm512_set_epi32(_Q(3), _Q(2), _Q(1), _Q(0)), tl);
			th = _mm512_permutexvar_epi16(_mm512_set_epi32(_Q(3), _Q(2), _Q(1), _Q(0)), th);
# undef _Q
# undef _P
#elif MWORD_SIZE == 32
			tl = _MM(i32gather_epi32)((int*)s16, _MM(set_epi32)(32, 40, 48, 56, 96, 104, 112, 120), 4);
			th = _MM(i32gather_epi32)((int*)s16, _MM(set_epi32)(0, 8, 16, 24, 64, 72, 80, 88), 4);
			/* 00001111 -> 00112233 */
			ta = _MM(packus_epi32)(
				_MMI(and)(tl, _MM(set1_epi32)(0xffff)),
				_MMI(and)(th, _MM(set1_epi32)(0xffff))
			);
			tb = _MM(packus_epi32)(
				_MM(srli_epi32)(tl, 16),
				_MM(srli_epi32)(th, 16)
			);
			tl = ta;
			th = tb;
#else
			/* MSVC _requires_ a constant so we have to manually unroll this loop */
			#define MM_INSERT(i) \
				tl = _MM(insert_epi16)(tl, s16[120 - i*8], i); \
				th = _MM(insert_epi16)(th, s16[ 56 - i*8], i)
			MM_INSERT(0);
			MM_INSERT(1);
			MM_INSERT(2);
			MM_INSERT(3);
			MM_INSERT(4);
			MM_INSERT(5);
			MM_INSERT(6);
			MM_INSERT(7);
			#undef MM_INSERT
#endif
			/* swizzle to [0123456789ABCDEF] [0123456789ABCDEF] */
			ta = _MM(packus_epi16)(
				_MM(srli_epi16)(tl, 8),
				_MM(srli_epi16)(th, 8)
			);
			tb = _MM(packus_epi16)(
				_MMI(and)(tl, lmask),
				_MMI(and)(th, lmask)
			);
			
#if MWORD_SIZE == 32
			ta = _mm256_permute4x64_epi64(ta, 0xD8); /* 3,1,2,0 */
			tb = _mm256_permute4x64_epi64(tb, 0xD8);
#endif
			
			/* extract top bits */
			dtmp[j*16 + 7] = MOVMASK(ta);
			dtmp[j*16 + 15] = MOVMASK(tb);
			for(i=1; i<8; i++) {
				ta = _MM(slli_epi16)(ta, 1);
				tb = _MM(slli_epi16)(tb, 1);
				dtmp[j*16 + 7-i] = MOVMASK(ta);
				dtmp[j*16 + 15-i] = MOVMASK(tb);
			}
			s16++;
		}
		/* we only really need to copy temp -> dest if src==dest */
#if MWORD_SIZE == 32 || MWORD_SIZE == 64
		/* ...but since we're copying anyway, may as well fix data arrangement! */
		for(j=0; j<16; j+=2) {
			ta = _MMI(load)((_mword*)dtmp +j);
			tb = _MMI(load)((_mword*)dtmp +1 +j);
#if MWORD_SIZE == 64
# define _P(a,b) (((a)<<16)|(b))
# define _Q(n) _P(28+n,24+n), _P(20+n,16+n), _P(12+n,8+n), _P(4+n,0+n)
			/* TODO: see if we can avoid permuting across both vectors by re-arranging earlier stuff */
			tl = _mm512_permutex2var_epi16(tb, _mm512_set_epi32(_Q(33), _Q(1), _Q(32), _Q(0)), ta);
			th = _mm512_permutex2var_epi16(tb, _mm512_set_epi32(_Q(35), _Q(3), _Q(34), _Q(2)), ta);
			ta = tl;
			tb = th;
# undef _Q
# undef _P
#endif
#if MWORD_SIZE == 32
			/* TODO: it should be possible to eliminate some permutes by storing things more efficiently */
			tl = _mm256_permute2x128_si256(ta, tb, 0x02);
			th = _mm256_permute2x128_si256(ta, tb, 0x13);
			ta = _MM(packus_epi32)(
				_MMI(and)(tl, _MM(set1_epi32)(0xffff)),
				_MMI(and)(th, _MM(set1_epi32)(0xffff))
			);
			tb = _MM(packus_epi32)(
				_MM(srli_epi32)(tl, 16),
				_MM(srli_epi32)(th, 16)
			);
#endif
			_MMI(storeu)((_mword*)d16 +j, ta);
			_MMI(storeu)((_mword*)d16 +1 +j, tb);
		}
#else
		memcpy(d16, dtmp, sizeof(dtmp));
#endif
		d16 += 128;
		s16 += 128 - 8; /*==15*8*/
	}
}

#undef umask_t
#undef PERMUTE_FIX_REV
#undef MOVMASK

static gf_val_32_t
#ifdef __GNUC__
__attribute__ ((unused))
#endif
_FN(gf_w16_xor_extract_word)(gf_t *gf, void *start, int bytes, int index)
{
  uint16_t *r16, rv = 0;
  uint8_t *r8;
  int i;
  gf_region_data rd;

  gf_set_region_data(&rd, gf, start, start, bytes, 0, 0, MWORD_SIZE, MWORD_SIZE*16);
  r16 = (uint16_t *) start;
  if (r16 + index < (uint16_t *) rd.d_start) return r16[index];
  if (r16 + index >= (uint16_t *) rd.d_top) return r16[index];
  
  index -= (((uint16_t *) rd.d_start) - r16);
  r8 = (uint8_t *) rd.d_start;
  r8 += (index & ~(MWORD_SIZE*8-1))*2; /* advance pointer to correct group */
  r8 += (index >> 3) & (MWORD_SIZE-1); /* advance to correct byte */
  for (i=0; i<16; i++) {
    rv <<= 1;
    rv |= (*r8 >> (7-(index & 7)) & 1);
    r8 += MWORD_SIZE;
  }
  return rv;
}


