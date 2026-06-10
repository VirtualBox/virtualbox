%ifidn __OUTPUT_FORMAT__,obj
section	code	use32 class=code align=64
%elifidn __OUTPUT_FORMAT__,win32
section	.text	code align=64
%else
section	.text	code
%endif
align	64
global	_poly1305_init
align	16
_poly1305_init:
L$_poly1305_init_begin:
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	edi,DWORD [20+esp]
	mov	esi,DWORD [24+esp]
	mov	ebp,DWORD [28+esp]
	xor	eax,eax
	mov	DWORD [edi],eax
	mov	DWORD [4+edi],eax
	mov	DWORD [8+edi],eax
	mov	DWORD [12+edi],eax
	mov	DWORD [16+edi],eax
	mov	DWORD [20+edi],eax
	cmp	esi,0
	je	NEAR L$000nokey
	mov	eax,DWORD [esi]
	mov	ebx,DWORD [4+esi]
	mov	ecx,DWORD [8+esi]
	mov	edx,DWORD [12+esi]
	and	eax,268435455
	and	ebx,268435452
	and	ecx,268435452
	and	edx,268435452
	mov	DWORD [24+edi],eax
	mov	DWORD [28+edi],ebx
	mov	DWORD [32+edi],ecx
	mov	DWORD [36+edi],edx
	mov	eax,0
L$000nokey:
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
global	_poly1305_blocks
align	16
_poly1305_blocks:
L$_poly1305_blocks_begin:
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	edi,DWORD [20+esp]
	mov	esi,DWORD [24+esp]
	mov	ecx,DWORD [28+esp]
L$001enter_blocks:
	and	ecx,-15
	jz	NEAR L$002nodata
	sub	esp,64
	mov	eax,DWORD [24+edi]
	mov	ebx,DWORD [28+edi]
	lea	ebp,[ecx*1+esi]
	mov	ecx,DWORD [32+edi]
	mov	edx,DWORD [36+edi]
	mov	DWORD [92+esp],ebp
	mov	ebp,esi
	mov	DWORD [36+esp],eax
	mov	eax,ebx
	shr	eax,2
	mov	DWORD [40+esp],ebx
	add	eax,ebx
	mov	ebx,ecx
	shr	ebx,2
	mov	DWORD [44+esp],ecx
	add	ebx,ecx
	mov	ecx,edx
	shr	ecx,2
	mov	DWORD [48+esp],edx
	add	ecx,edx
	mov	DWORD [52+esp],eax
	mov	DWORD [56+esp],ebx
	mov	DWORD [60+esp],ecx
	mov	eax,DWORD [edi]
	mov	ebx,DWORD [4+edi]
	mov	ecx,DWORD [8+edi]
	mov	esi,DWORD [12+edi]
	mov	edi,DWORD [16+edi]
	jmp	NEAR L$003loop
align	32
L$003loop:
	add	eax,DWORD [ebp]
	adc	ebx,DWORD [4+ebp]
	adc	ecx,DWORD [8+ebp]
	adc	esi,DWORD [12+ebp]
	lea	ebp,[16+ebp]
	adc	edi,DWORD [96+esp]
	mov	DWORD [esp],eax
	mov	DWORD [12+esp],esi
	mul	DWORD [36+esp]
	mov	DWORD [16+esp],edi
	mov	edi,eax
	mov	eax,ebx
	mov	esi,edx
	mul	DWORD [60+esp]
	add	edi,eax
	mov	eax,ecx
	adc	esi,edx
	mul	DWORD [56+esp]
	add	edi,eax
	mov	eax,DWORD [12+esp]
	adc	esi,edx
	mul	DWORD [52+esp]
	add	edi,eax
	mov	eax,DWORD [esp]
	adc	esi,edx
	mul	DWORD [40+esp]
	mov	DWORD [20+esp],edi
	xor	edi,edi
	add	esi,eax
	mov	eax,ebx
	adc	edi,edx
	mul	DWORD [36+esp]
	add	esi,eax
	mov	eax,ecx
	adc	edi,edx
	mul	DWORD [60+esp]
	add	esi,eax
	mov	eax,DWORD [12+esp]
	adc	edi,edx
	mul	DWORD [56+esp]
	add	esi,eax
	mov	eax,DWORD [16+esp]
	adc	edi,edx
	imul	eax,DWORD [52+esp]
	add	esi,eax
	mov	eax,DWORD [esp]
	adc	edi,0
	mul	DWORD [44+esp]
	mov	DWORD [24+esp],esi
	xor	esi,esi
	add	edi,eax
	mov	eax,ebx
	adc	esi,edx
	mul	DWORD [40+esp]
	add	edi,eax
	mov	eax,ecx
	adc	esi,edx
	mul	DWORD [36+esp]
	add	edi,eax
	mov	eax,DWORD [12+esp]
	adc	esi,edx
	mul	DWORD [60+esp]
	add	edi,eax
	mov	eax,DWORD [16+esp]
	adc	esi,edx
	imul	eax,DWORD [56+esp]
	add	edi,eax
	mov	eax,DWORD [esp]
	adc	esi,0
	mul	DWORD [48+esp]
	mov	DWORD [28+esp],edi
	xor	edi,edi
	add	esi,eax
	mov	eax,ebx
	adc	edi,edx
	mul	DWORD [44+esp]
	add	esi,eax
	mov	eax,ecx
	adc	edi,edx
	mul	DWORD [40+esp]
	add	esi,eax
	mov	eax,DWORD [12+esp]
	adc	edi,edx
	mul	DWORD [36+esp]
	add	esi,eax
	mov	ecx,DWORD [16+esp]
	adc	edi,edx
	mov	edx,ecx
	imul	ecx,DWORD [60+esp]
	add	esi,ecx
	mov	eax,DWORD [20+esp]
	adc	edi,0
	imul	edx,DWORD [36+esp]
	add	edx,edi
	mov	ebx,DWORD [24+esp]
	mov	ecx,DWORD [28+esp]
	mov	edi,edx
	shr	edx,2
	and	edi,3
	lea	edx,[edx*4+edx]
	add	eax,edx
	adc	ebx,0
	adc	ecx,0
	adc	esi,0
	adc	edi,0
	cmp	ebp,DWORD [92+esp]
	jne	NEAR L$003loop
	mov	edx,DWORD [84+esp]
	add	esp,64
	mov	DWORD [edx],eax
	mov	DWORD [4+edx],ebx
	mov	DWORD [8+edx],ecx
	mov	DWORD [12+edx],esi
	mov	DWORD [16+edx],edi
L$002nodata:
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
global	_poly1305_emit
align	16
_poly1305_emit:
L$_poly1305_emit_begin:
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	ebp,DWORD [20+esp]
L$004enter_emit:
	mov	edi,DWORD [24+esp]
	mov	eax,DWORD [ebp]
	mov	ebx,DWORD [4+ebp]
	mov	ecx,DWORD [8+ebp]
	mov	edx,DWORD [12+ebp]
	mov	esi,DWORD [16+ebp]
	add	eax,5
	adc	ebx,0
	adc	ecx,0
	adc	edx,0
	adc	esi,0
	shr	esi,2
	neg	esi
	and	eax,esi
	and	ebx,esi
	and	ecx,esi
	and	edx,esi
	mov	DWORD [edi],eax
	mov	DWORD [4+edi],ebx
	mov	DWORD [8+edi],ecx
	mov	DWORD [12+edi],edx
	not	esi
	mov	eax,DWORD [ebp]
	mov	ebx,DWORD [4+ebp]
	mov	ecx,DWORD [8+ebp]
	mov	edx,DWORD [12+ebp]
	mov	ebp,DWORD [28+esp]
	and	eax,esi
	and	ebx,esi
	and	ecx,esi
	and	edx,esi
	or	eax,DWORD [edi]
	or	ebx,DWORD [4+edi]
	or	ecx,DWORD [8+edi]
	or	edx,DWORD [12+edi]
	add	eax,DWORD [ebp]
	adc	ebx,DWORD [4+ebp]
	adc	ecx,DWORD [8+ebp]
	adc	edx,DWORD [12+ebp]
	mov	DWORD [edi],eax
	mov	DWORD [4+edi],ebx
	mov	DWORD [8+edi],ecx
	mov	DWORD [12+edi],edx
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
db	80,111,108,121,49,51,48,53,32,102,111,114,32,120,56,54
db	44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32
db	60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111
db	114,103,62,0
align	4
