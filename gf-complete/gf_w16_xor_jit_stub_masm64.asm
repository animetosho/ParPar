PUBLIC  gf_w16_xor_jit_stub
.code
gf_w16_xor_jit_stub PROC
	; setup stack
	push rsi
	lea rax, [rsp-16*12]
	mov rsi, rax
	and rax, 0Fh
	sub rsi, rax
	
	; move registers to what we need them as
	mov rax, rcx
	mov rcx, rdx
	mov rdx, r8
	
	; save XMM registers
	movaps [rsi+16*1], xmm6
	movaps [rsi+16*2], xmm7
	movaps [rsi+16*3], xmm8
	movaps [rsi+16*4], xmm9
	movaps [rsi+16*5], xmm10
	movaps [rsi+16*6], xmm11
	movaps [rsi+16*7], xmm12
	movaps [rsi+16*8], xmm13
	movaps [rsi+16*9], xmm14
	movaps [rsi+16*10], xmm15
	
	; run JIT code
	call r9
	
	; restore XMM registers
	movaps xmm6, [rsi+16*1]
	movaps xmm7, [rsi+16*2]
	movaps xmm8, [rsi+16*3]
	movaps xmm9, [rsi+16*4]
	movaps xmm10, [rsi+16*5]
	movaps xmm11, [rsi+16*6]
	movaps xmm12, [rsi+16*7]
	movaps xmm13, [rsi+16*8]
	movaps xmm14, [rsi+16*9]
	movaps xmm15, [rsi+16*10]
	
	pop rsi
	ret

gf_w16_xor_jit_stub    ENDP
END
