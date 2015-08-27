

/* src can be the same as dest */
static void _FN(gf_w16_split_start)(void* src, int bytes, void* dest) {
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword ta, tb, lmask;
	
	gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, sizeof(_mword)*2);
	
	
	if(src != dest) {
		/* copy end and initial parts */
		memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
		memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
	}
	
	sW = (_mword*)rd.s_start;
	dW = (_mword*)rd.d_start;
	topW = (_mword*)rd.d_top;
	
	lmask = _MM(set1_epi16) (0xff);
	
	while(dW != topW) {
		ta = _MMI(load_s)( sW);
		tb = _MMI(load_s)(sW+1);
		
		_MMI(store_s) (dW,
			_MM(packus_epi16)(
				_MM(srli_epi16)(tb, 8),
				_MM(srli_epi16)(ta, 8)
			)
		);
		_MMI(store_s) (dW+1,
			_MM(packus_epi16)(
				_MMI(and_s)(tb, lmask),
				_MMI(and_s)(ta, lmask)
			)
		);
		
		sW += 2;
		dW += 2;
	}
}

/* src can be the same as dest */
static void _FN(gf_w16_split_final)(void* src, int bytes, void* dest) {
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword tpl, tph;
	
	gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, sizeof(_mword)*2);
	
	
	if(src != dest) {
		/* copy end and initial parts */
		memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
		memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
	}
	
	sW = (_mword*)rd.s_start;
	dW = (_mword*)rd.d_start;
	topW = (_mword*)rd.d_top;
	
	while(dW != topW) {
		tph = _MMI(load_s)( sW);
		tpl = _MMI(load_s)(sW+1);

		_MMI(store_s) (dW, _MM(unpackhi_epi8)(tpl, tph));
		_MMI(store_s) (dW+1, _MM(unpacklo_epi8)(tpl, tph));
		
		sW += 2;
		dW += 2;
	}
}
