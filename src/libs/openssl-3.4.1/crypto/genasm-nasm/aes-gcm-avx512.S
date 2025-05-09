default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
section	.text code align=64

global	ossl_vaes_vpclmulqdq_capable

ossl_vaes_vpclmulqdq_capable:
	xor	eax,eax
	DB	0F3h,0C3h		;repret


global	ossl_aes_gcm_init_avx512
global	ossl_aes_gcm_setiv_avx512
global	ossl_aes_gcm_update_aad_avx512
global	ossl_aes_gcm_encrypt_avx512
global	ossl_aes_gcm_decrypt_avx512
global	ossl_aes_gcm_finalize_avx512
global	ossl_gcm_gmult_avx512


ossl_aes_gcm_init_avx512:
ossl_aes_gcm_setiv_avx512:
ossl_aes_gcm_update_aad_avx512:
ossl_aes_gcm_encrypt_avx512:
ossl_aes_gcm_decrypt_avx512:
ossl_aes_gcm_finalize_avx512:
ossl_gcm_gmult_avx512:
DB	0x0f,0x0b
	DB	0F3h,0C3h		;repret

