;		run time for 6502 C compiler
; The variables
 
;Host processor
STACK = $5000
 *= $1900
 
;2nd processor
;STACK = $E000
; *= $800

r0 = 0
r1 = 4
r2 = 8
r3 = 12
r4 = 16
r5 = 20
r6 = 24
r7 = 28
r8 = 32
r9 = 36
r10 = 40
r11 = 44
r12 = 48
fp = 52
ap = 54
sp = 66
tmp = 68
tmp1 = 72 ; this is sometimes overwritten by tmp
tmp2 = 76
tmp3 = 80
retstack = 84

 LDA #<STACK
 STA sp
 LDA #>STACK	;
 STA sp+1

 TSX
 STX retstack

 JMP _main

; These are needed by the library functions to access the MOS
param =$0080
userv =$0200
brkv =$0202
irq1v =$0204
irq2v =$0206
cliv =$0208
bytev =$020A
wordv =$020C
wrchv =$020E
rdchv =$0210
filev =$0212
argsv =$0214
bgetv =$0216
bputv =$0218
gbpbv =$021A
findv =$021C
fscv =$021E
evntv =$0220
uptv =$0222
netv =$0224
vduv =$0226
keyv =$0228
insv =$022A
remv =$022C
cnpv =$022E
ind1v =$0230
ind2v =$0232
ind3v =$0234
gsbuf =$0400
osurom =$8000
osrdrm =$FFB9
oseven =$FFBF
gsinit =$FFC2
gsread =$FFC5
nvwrch =$FFC8
nvrdch =$FFCB
osfind =$FFCE
osgbpb =$FFD1
osbput =$FFD4
osbget =$FFD7
osargs =$FFDA
osfile =$FFDD
osrdch =$FFE0
osasci =$FFE3
osnewl =$FFE7
oswrch =$FFEE
osword =$FFF1
osbyte =$FFF4
oscli =$FFF7

; Some Maths routines

; 32bit multply tmp+4 x tmp2 -> tmp
mult
	ldy #32
mult1
	asl tmp
	rol tmp+1
	rol tmp+2
	rol tmp+3
	rol tmp+4
	rol tmp+5
	rol tmp+6
	rol tmp+7
	bcc mult2
	clc
	lda tmp2
	adc tmp
	sta tmp
	lda tmp2+1
	adc tmp+1
	sta tmp+1
	lda tmp2+2
	adc tmp+2
	sta tmp+2
	lda tmp2+3
	adc tmp+3
	sta tmp+3
	lda #0
	adc tmp+4
	sta tmp+4
mult2
	dey
	bne mult1
	rts

;
; tmp2=tmp2/tmp (32-bit signed)
;
div
	lda tmp+3	;dividend sign
	eor tmp2+3
	pha		;sign of quotient
	
	lda tmp+3	;test sign of xy
	bpl div1
	sec
	lda #0
	sbc tmp
	sta tmp
	lda #0
	sbc tmp+1
	sta tmp+1
	lda #0
	sbc tmp+2
	sta tmp+2
	lda #0
	sbc tmp+3
	sta tmp+3
div1
	lda tmp2+3	;test sign of xy
	bpl div2
	sec
	lda #0
	sbc tmp2
	sta tmp2
	lda #0
	sbc tmp2+1
	sta tmp2+1
	lda #0
	sbc tmp2+2
	sta tmp2+2
	lda #0
	sbc tmp2+3
	sta tmp2+3
div2
	jsr udiv	;unsigned divide
	
	pla		;sign of quotient
	bpl div3
	sec
	lda #0
	sbc tmp2
	sta tmp2
	lda #0
	sbc tmp2+1
	sta tmp2+1
	lda #0
	sbc tmp2+2
	sta tmp2+2
	lda #0
	sbc tmp2+3
	sta tmp2+3
div3
	rts


;
; tmp2=tmp2/tmp (32-bit unsigned)
;
udiv
	jsr ztrap	;trap division by zero

	lda #0
	sta tmp1
	sta tmp1+1
	sta tmp1+2
	sta tmp1+3
	
	ldx #32
	asl tmp2
	rol tmp2+1
	rol tmp2+2
	rol tmp2+3
udiv2
	rol tmp1
	rol tmp1+1
	rol tmp1+2
	rol tmp1+3
	sec
	lda tmp1
	sbc tmp
	sta tmp3
	lda tmp1+1
	sbc tmp+1
	sta tmp3+1
	lda tmp1+2
	sbc tmp+2
	sta tmp3+2
	lda tmp1+3
	sbc tmp+3
	bcc udiv3
	
	sta tmp1+3
	lda tmp3+2
	sta tmp1+2
	lda tmp3+1
	sta tmp1+1
	lda tmp3
	sta tmp1
udiv3
	rol tmp2
	rol tmp2+1
	rol tmp2+2
	rol tmp2+3
	dex
	bne udiv2
	rts


;
; tmp2=tmp2 % tmp (32-bit signed)
;
mod
	lda tmp+3	;dividend sign
	eor tmp2+3
	pha		;sign of quotient
	
	lda tmp+3	;test sign of xy
	bpl mod1
	sec
	lda #0
	sbc tmp
	sta tmp
	lda #0
	sbc tmp+1
	sta tmp+1
	lda #0
	sbc tmp+2
	sta tmp+2
	lda #0
	sbc tmp+3
	sta tmp+3
mod1
	lda tmp2+3	;test sign of xy
	bpl mod2
	sec
	lda #0
	sbc tmp2
	sta tmp2
	lda #0
	sbc tmp2+1
	sta tmp2+1
	lda #0
	sbc tmp2+2
	sta tmp2+2
	lda #0
	sbc tmp2+3
	sta tmp2+3
mod2
	jsr udiv	;unsigned divide
	
	pla		;sign of quotient
	bpl mod3
	sec
	lda #0
	sbc tmp1
	sta tmp1
	lda #0
	sbc tmp1+1
	sta tmp1+1
	lda #0
	sbc tmp1+2
	sta tmp1+2
	lda #0
	sbc tmp1+3
	sta tmp1+3
mod3
	rts




ztrap
	lda tmp
	bne ztrap1
	lda tmp+1
	bne ztrap1
	lda tmp+2
	bne ztrap1
	lda tmp+3
	bne ztrap1
	brk
	.byte 0
	.byte $64	;"division by zero"
	.byte $69
	.byte $76
	.byte $69
	.byte $73
	.byte $69
	.byte $6f
	.byte $6e
	.byte $20
	.byte $62
	.byte $79
	.byte $20
	.byte $7a
	.byte $65
	.byte $72
	.byte $6f
	.byte 0
ztrap1
	rts


;some system call type functions, largly ripped of tcc
;
; Operating system interface for tcc under Acorn/BBC MOS
;
;

;
; exit()
; ------
; exit from user program
;
_exit
	LDX retstack
	TXS
	RTS

;
; open(name, rwmode)
; ------------------
; exit with error status if file does not exist
; new files are opened with creat()
; MOS modes: read=$40, write=$80, r/w=$c0
;
_open
	jsr gsconv	;convert filename
	lda #<gsbuf	;MOS filename
	sta param	;file control block
	lda #>gsbuf
	sta param+1
	lda #5		;read catalogue information
	ldx #<param
	ldy #>param
	jsr osfile
	cmp #0
	beq openerr	;if zero, file does not exist
;
	ldy #2		;char* are 16bit
	lda (sp),y	;rwmode lo
	clc
	adc #1		;convert to MOS format
	ror a
	ror a
	ror a
	ldx #<gsbuf	;filename
	ldy #>gsbuf
	jsr osfind
	sta r0		;return fd in r0
	beq openerr	;unless zero file handle
;
	jmp retzero
openerr
	jmp reterr


;
; creat(name, pmode)
; ------------------
; pmode ignored in this version ...
;
_creat
	jsr gsconv
	lda #$80	;open in write mode
	ldx #<gsbuf	;filename
	ldy #>gsbuf
	jsr osfind
	sta r0		;return fd in xy
	beq createrr	;unless zero file handle
;
	jmp retzero
createrr
	jmp reterr

;
; close(fd)
;
_close
	ldy #0
	lda (sp),y	;fd lo
	tay		;MOS fd parameter
	lda #0		;close file operation
	jsr osfind
	rts

;
; unlink(name)
;
_unlink
	jsr gsconv
	lda #<gsbuf	;filename
	sta param	;control block
	lda #>gsbuf
	sta param+1
	lda #6		;delete file
	ldx #<param
	ldy #>param
	jsr osfile
	rts

;
; system(string)
; --------------
; execute MOS command
;
_system
	jsr gsconv	;convert to MOS format
	ldx #<gsbuf
	ldy #>gsbuf
	jsr oscli
	rts

;
; convert string from C to MOS format
; -----------------------------------
; string is null terminated in c, $0d terminated in MOS
; use pr to copy filename from addr pointed to by
; top stack item into MOS general string input buffer
;
gsconv
	ldy #0
	lda (sp),y	;name lo
	sta tmp
	iny
	lda (sp),y	;name hi
	sta tmp+1
	ldy #0
conv
	lda (tmp),y
	sta gsbuf,y	;parameter block for call
	beq term
	iny
	bne conv	;max length = 255 chars
term
	lda #13		;replaces C '\0'
	sta gsbuf,y
	rts


;
; read(fd, buf, count)
; --------------------
; checks eof on fd before attempting to read
;
_read
	ldy #0
	lda (sp),y	;fd
	tax		;MOS fd in x
	lda #127	;check eof operation
	jsr osbyte
	txa		
	beq doread
	jmp retzero
doread
	lda #4
	pha
	jmp access

;
; write(fd, buf, count)
;
_write
	ldy #0
	lda (sp),y	;fd
	cmp #1		;fd 1 is stdout
	beq sput
	cmp #2		;fd 2 is stderr
	beq sput
	lda #2
	pha
	jmp access

;
; string output on stdout or stderr
;
sput
	ldy #4
	lda (sp),y	;buf lo
	sta tmp
	iny
	lda (sp),y	;buf hi
	sta tmp+1
	ldy #6
	lda (sp),y	;count lo
	sta tmp1
	iny
	lda (sp),y	;count hi
	tax
	ldy #0
sput1
	lda tmp1
	bne sput2	 	
	cpx #0
	beq sput3
	dex
sput2
	dec tmp1
	lda (tmp),y
	jsr aput
	iny
	bne sput1
	inc tmp+1
	jmp sput1
sput3
	rts	
;
; MOS file access interface to tcc
; --------------------------------
; on entry:
;	a=2: write, ignoring new file ptr
;	a=4: read, ignoring new file ptr
;
; high order buffer address is zero, so the 6502 second processor
; accesses the 'correct' buffer in second processor memory.
;
access
	ldy #0
	lda (sp),y	;fd lo
	sta param
	ldy #4
	lda (sp),y	;buf addr lo
	sta param+1
	iny
	lda (sp),y	;buf addr hi
	sta param+2
	lda #0		;hi order buf addr bytes
	sta param+3
	sta param+4
	ldy #6
	lda (sp),y	;count lo
	sta param+5
	iny
	lda (sp),y	;count hi
	sta param+6
	lda #0
	sta param+7	;zero high order count
	sta param+8
	pla		;get access mode
	ldx #<param	;MOS control block
	ldy #>param
	jsr osgbpb
	sec		;calc no. transfers
	ldy #8
	lda (sp),y	;count lo
	sbc param+5	;resid lo
	sta r0		;ntransfers lo
	iny
	lda (sp),y	;count hi
	sbc param+6	;resid hi
	sta r0+1	;ntransfers hi
	lda #0
	sta r0+2
	sta r0+3
	rts

;
; getc(fd)
;
_getc
	ldy #0
	lda (sp),y	;fd lo
	beq _getchar	;fd 0 is stdin
	tay		;MOS fd in y
	jsr osbget
	bcs geof	;c indicates eof on fd
	sta r0
	lda #0
	sta r0+1
	sta r0+2
	sta r0+3
	rts
geof
	jmp reterr
;
; input char from keyboard/rs423
;
_getchar
	jsr osrdch
	cmp #$0d	;\r
	bne echo
	jsr oswrch	;echo \r on input
	lda #$0a	;map \r to \n on input
echo
	jsr oswrch	;echo chars on input
	sta r0
	lda #0
	sta r0+1
	sta r0+2
	sta r0+3
	rts

;
; putc(c, fd)
;
_putc
	ldy #0
	lda (sp),y	;char to put
	tax		;save in x for now
	ldy #4		;???????
	lda (sp),y	;fd
	cmp #1		;fd 1 is stdout
	beq xput
	cmp #2		;fd 2 is stderr
	beq xput
	tay		;MOS fd in y
	txa		;MOS char in a
	jsr osbput
	rts
;
; output x to screen/rs423
;
xput
	txa
;
; output a to screen/rs423
;
aput
	cmp #$0a	;\n
	beq mapn   
	jsr oswrch
	rts
;
; map \n to \r\n on output
;
mapn
	lda #$0d	;\r
	jsr oswrch
	lda #$0a	;\n
	jsr oswrch
	rts
;
; vdu(c)
; ------
; output char to keyboard/rs423 without \n to \r\n transformation
;
_vdu
	ldy #0
	lda (sp),y
	jsr oswrch
	rts

;
; osbyte(type, param,param)
; ------------------------
; MOS "osbyte" interface
; a is type of call
; x and y are parameters
;
_osbyte
	ldy #4		;????
	lda (sp),y	;x parameter
	tax
	ldy #8
	lda (sp),y	;y parameter
	sta tmp		;save it
	ldy #0
	lda (sp),y	;type of call
	ldy tmp
	jsr osbyte
	stx r0
	sty r0+1
	lda #0
	sta r0+2
	sta r0+3
	rts		;return val in xy

;
; osword(type, address)
; ---------------------
; MOS "osword" interface
; a is type of call
; (x + y << 8) is address of parameter block
;
_osword
	ldy #4		;????
	lda (sp),y	;address lo
	tax
	iny
	lda (sp),y	;address hi
	sta tmp		;save it
	ldy #0
	lda (sp),y	;type of call
	ldy tmp
	jsr osword
	stx r0
	sty r0+1
	lda #0
	sta r0+2
	sta r0+3
	rts		;return val in xy
	
;
; stat(name, fcb)
;
_stat
	lda #5		;read catalogue information
	jmp osfile0
;
	
;
; osfile(name, fcb, type)
; -----------------------
; MOS osfile interface
;
_osfile
	ldy #4
	lda (sp),y	;osfile type
osfile0
	pha		;save type
	jsr gsconv	;convert name to MOS format
	ldy #2
	lda (sp),y	;fcb address lo
	sta tmp
	iny
	lda (sp),y	;fcb address hi
	sta tmp+1
	ldy #0
	lda #<gsbuf	;update file control block
	sta (tmp),y	;fcb address lo
	iny
	lda #>gsbuf
	sta (tmp),y	;fcb address hi
	pla		;osfile type
	ldx tmp		;addr of fcb
	ldy tmp+1
	jsr osfile
	cmp #0		;check MOS exit status, 0 = no file
	bne osfile1
	jmp reterr

osfile1
	sta (tmp),y	;leave MOS file type in fcb 
	jmp retzero

reterr
	lda #$ff	;return -1
	sta r0
	sta r0+1
	sta r0+2
	sta r0+3
	rts
	
retzero
false
	lda #0		;return 0
	sta r0
	sta r0+1
	sta r0+2
	sta r0+3
	rts

true
	lda #1		;return 1
	sta r0
	lda #0
	sta r0+1
	sta r0+2
	sta r0+3
	rts

;
; isalpha(c)
;
_isalpha
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$03	;_U | _L
	beq isalpha1
	jmp true
isalpha1
	jmp false

;
; isupper(c)
;
_isupper
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$01	;_U
	beq isupper1
	jmp true
isupper1
	jmp false



;
; islower(c)
;
_islower
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$02	;_L
	beq islower1
	jmp true
islower1
	jmp false
;
; isdigit(c)
;
_isdigit
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$04	;_N
	beq isdigit1
	jmp true
isdigit1
	jmp false
;
; isxdigit(c)
;
_isxdigi
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$44	;_N | _X
	beq isxdigit1
	jmp true
isxdigit1
	jmp false
;
; isspace(c)
;
_isspace
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$08	;_S
	beq isspace1
	jmp true
isspace1
	jmp false
;
; ispunct(c)
;
_ispunct
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$10	;_P
	beq ispunct1
	jmp true
ispunct1
	jmp false
;
; isalnum(c)
;
_isalnum
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$07	;_U | _L | _N
	beq isalnum1
	jmp true
isalnum1
	jmp false
;
; isprint(c)
;
_isprint
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$17	;_P | _U | _L | _N
	beq isprint1
	jmp true
isprint1
	jmp false
;
; iscntrl(c)
;
_iscntrl
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$20	;_C
	beq iscntrl1
	jmp true
iscntrl1
	jmp false
;
; isascii(c)
;
_isascii
	ldy #0
	lda (sp),y	;low byte of c
	and #$80	;0 if <= 127
	eor #$80	;invert
	beq isascii1
	jmp true
isascii1
	jmp false

;
; toupper(c)
;
_toupper
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$02	;_L
	beq toupper1	;skip if not lower-case
	sec
	txa		;original char
	sbc #'a' - 'A'	;force upper case
	tax
toupper1
	stx r0
	rts

;
; tolower(c)
;
_tolower
	ldy #0
	lda (sp),y	;low byte of c
	tax
	lda _ctype_,x
	and #$01	;_U
	beq tolower1	;skip if not upper-case
	clc
	txa		;original char
	adc #'a' - 'A'	;force lower case
	tax
tolower1
	stx r0
	lda #0
	rts

;
; toascii(c)
;
_toascii
	ldy #0
	lda (sp),y	;low byte of c
	and #$7f
	sta r0
	lda #0
	sta r0+1
	sta r0+2
	sta r0+3
	rts
	
;
; character classification table
;
	.byte $00	;EOF 'subscript' is -1
_ctype_
	.byte $20,$20,$20,$20,$20,$20,$20,$20
	.byte $20,$08,$08,$08,$08,$08,$20,$20
	.byte $20,$20,$20,$20,$20,$20,$20,$20
	.byte $20,$20,$20,$20,$20,$20,$20,$20
	.byte $18,$10,$10,$10,$10,$10,$10,$10
	.byte $10,$10,$10,$10,$10,$10,$10,$10
	.byte $04,$04,$04,$04,$04,$04,$04,$04
	.byte $04,$04,$10,$10,$10,$10,$10,$10
	.byte $10,$41,$41,$41,$41,$41,$41,$01
	.byte $01,$01,$01,$01,$01,$01,$01,$01
	.byte $01,$01,$01,$01,$01,$01,$01,$01
	.byte $01,$01,$01,$10,$10,$10,$10,$10
	.byte $10,$42,$42,$42,$42,$42,$42,$02
	.byte $02,$02,$02,$02,$02,$02,$02,$02
	.byte $02,$02,$02,$02,$02,$02,$02,$02
	.byte $02,$02,$02,$10,$10,$10,$10,$20
;

;
; export address of MOS command line to C environment
;
__cmdlin
	lda #1		;get address of MOS command line		
	ldx #param	;4 byte parameter block
	ldy #0		;no file descriptor involved
	jsr osargs
	lda param	;return address of '\r' terminated line
	sta r0
	lda param+1
	sta r0+1
	lda param+2
	sta r0+2
	lda param+3
	sta r0+3
	rts

;
; brk.c - the brk system call.
;
; brk is used to expand the data space used by the program
; is tests the pointer passed to it to make sure that it is
; below the stack and sets the new top to this value.
; The old top value is returned. If no more memory is available
; the -1 is returned.
;
curptr .word _end
_sbrk			; Add curptr to the required number of bytes
	ldy #0		; then drop through to brk
	clc
	lda (sp),y
	adc curptr
	sta (sp),y
	iny
	lda (sp),y
	adc curptr+1
	sta (sp),y
	
_brk	
	ldy #0
	lda (sp),y
	cmp sp
	iny
	lda (sp),y
	cmp sp+1

	bcc chstkok
	jmp reterr
chstkok	
	lda curptr
	sta r0
	lda curptr+1
	sta r0+1
	
	ldy #0
	lda (sp),y
	sta curptr
	iny
	lda (sp),y
	sta curptr+1
	rts
	

;char *strcpy(s1, s2)
_strcpy
	ldy #0
	lda (sp),y
	sta tmp
	sta r0
	iny
	lda (sp),y
	sta tmp+1
	sta r0+1

	ldy #2
	lda (sp),y
	sta tmp1
	iny
	lda (sp),y
	sta tmp1+1

	ldy #0
strcpyloop
	lda (tmp1),Y
	sta (tmp),Y
	beq strcpyend
	iny
	bne strcpyloop
	inc tmp
	inc tmp1
	jmp strcpyloop
strcpyend
	rts


;int strcmp(s1, s2)
_strcmp
	ldy #0
	lda (sp),y
	sta tmp
	iny
	lda (sp),y
	sta tmp+1

	ldy #2
	lda (sp),y
	sta tmp1
	iny
	lda (sp),y
	sta tmp1+1

	ldy #0
strcmploop
	lda (tmp),Y
	cmp (tmp1),Y
	bne strcmpend
	cmp #0
	beq strcmpend0
	iny
	bne strcmploop
	inc tmp
	inc tmp1
	jmp strcmploop

strcmpend0
	jmp retzero
strcmpend
	sec
	sbc (tmp1),Y
	sta r0
	bcs strcmp1
	lda #$ff
	sta r0+1
	sta r0+2
	sta r0+3
	rts
strcmp1
	lda #0
	sta r0+1
	sta r0+2
	sta r0+3
	rts
