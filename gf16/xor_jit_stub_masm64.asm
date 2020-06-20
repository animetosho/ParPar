IFDEF RAX

.code

PUBLIC  gf16_xor_jit_stub
gf16_xor_jit_stub PROC
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
gf16_xor_jit_stub    ENDP

IFDEF YMM0
PUBLIC  gf16_xor256_jit_stub
gf16_xor256_jit_stub PROC
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
	vmovaps [rsi+16*1], xmm6
	vmovaps [rsi+16*2], xmm7
	vmovaps [rsi+16*3], xmm8
	vmovaps [rsi+16*4], xmm9
	vmovaps [rsi+16*5], xmm10
	vmovaps [rsi+16*6], xmm11
	vmovaps [rsi+16*7], xmm12
	vmovaps [rsi+16*8], xmm13
	vmovaps [rsi+16*9], xmm14
	vmovaps [rsi+16*10], xmm15
	
	; run JIT code
	call r9
	
	; restore XMM registers
	vmovaps xmm6, [rsi+16*1]
	vmovaps xmm7, [rsi+16*2]
	vmovaps xmm8, [rsi+16*3]
	vmovaps xmm9, [rsi+16*4]
	vmovaps xmm10, [rsi+16*5]
	vmovaps xmm11, [rsi+16*6]
	vmovaps xmm12, [rsi+16*7]
	vmovaps xmm13, [rsi+16*8]
	vmovaps xmm14, [rsi+16*9]
	vmovaps xmm15, [rsi+16*10]
	
	pop rsi
	ret
gf16_xor256_jit_stub    ENDP

PUBLIC  gf16_xor256_jit_multi_stub
gf16_xor256_jit_multi_stub PROC
	; non-volatile regs
	push rsi
	push rdi
	push rbx
	push r12
	push r13
	push r14
	push r15
	push rbp
	; setup stack
	lea rax, [rsp-16*12]
	mov rbp, rax
	and rax, 0Fh
	sub rbp, rax
	
	; save XMM registers
	vmovaps [rbp+16*1], xmm6
	vmovaps [rbp+16*2], xmm7
	vmovaps [rbp+16*3], xmm8
	vmovaps [rbp+16*4], xmm9
	vmovaps [rbp+16*5], xmm10
	vmovaps [rbp+16*6], xmm11
	vmovaps [rbp+16*7], xmm12
	vmovaps [rbp+16*8], xmm13
	vmovaps [rbp+16*9], xmm14
	vmovaps [rbp+16*10], xmm15
	
	; move registers to what we need them as
	mov rax, rcx ; dst
	mov rcx, rdx ; dstEnd
	mov rbx, r9  ; fn
	
	; load src pointers into registers
	mov rdx, [r8]
	mov rsi, [r8+8]
	mov rdi, [r8+16]
	mov r9 , [r8+32]
	mov r10, [r8+40]
	mov r11, [r8+48]
	mov r12, [r8+56]
	mov r13, [r8+64]
	mov r14, [r8+72]
	mov r15, [r8+80]
	mov r8 , [r8+24]
	
	; run JIT code
	call rbx
	
	; restore XMM registers
	vmovaps xmm6, [rbp+16*1]
	vmovaps xmm7, [rbp+16*2]
	vmovaps xmm8, [rbp+16*3]
	vmovaps xmm9, [rbp+16*4]
	vmovaps xmm10, [rbp+16*5]
	vmovaps xmm11, [rbp+16*6]
	vmovaps xmm12, [rbp+16*7]
	vmovaps xmm13, [rbp+16*8]
	vmovaps xmm14, [rbp+16*9]
	vmovaps xmm15, [rbp+16*10]
	
	pop rbp
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbx
	pop rdi
	pop rsi
	ret
gf16_xor256_jit_multi_stub    ENDP
ENDIF ; IFDEF YMM0

ENDIF ; IFDEF RAX

END
