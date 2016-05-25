PUBLIC  gf_w16_xor_jit_stub
.code
gf_w16_xor_jit_stub PROC
	; setup stack
	push rbx
	lea rbx, [rsp-16*12]
	mov rax, rbx
	and rax, 0Fh
	sub rbx, rax
	
	; move registers to what we need them as
	mov rax, rcx
	mov rcx, rdx
	mov rdx, r8
	
	; save XMM registers
	movaps [rbx+16*1], xmm6
	movaps [rbx+16*2], xmm7
	movaps [rbx+16*3], xmm8
	movaps [rbx+16*4], xmm9
	movaps [rbx+16*5], xmm10
	movaps [rbx+16*6], xmm11
	movaps [rbx+16*7], xmm12
	movaps [rbx+16*8], xmm13
	movaps [rbx+16*9], xmm14
	movaps [rbx+16*10], xmm15
	
	; run JIT code
	call r9
	
	; restore XMM registers
	movaps xmm6, [rbx+16*1]
	movaps xmm7, [rbx+16*2]
	movaps xmm8, [rbx+16*3]
	movaps xmm9, [rbx+16*4]
	movaps xmm10, [rbx+16*5]
	movaps xmm11, [rbx+16*6]
	movaps xmm12, [rbx+16*7]
	movaps xmm13, [rbx+16*8]
	movaps xmm14, [rbx+16*9]
	movaps xmm15, [rbx+16*10]
	
	pop rbx
	ret

gf_w16_xor_jit_stub    ENDP
END
