default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
section	.text code align=64


global	ossl_rsaz_amm52x40_x1_ifma256
global	ossl_rsaz_amm52x40_x2_ifma256
global	ossl_extract_multiplier_2x40_win5

ossl_rsaz_amm52x40_x1_ifma256:
ossl_rsaz_amm52x40_x2_ifma256:
ossl_extract_multiplier_2x40_win5:
DB	0x0f,0x0b
	DB	0F3h,0C3h		;repret

