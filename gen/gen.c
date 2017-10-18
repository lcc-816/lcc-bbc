/* C compiler: 6502 subset code generator */

#include "c.h"
#include <stdio.h>

#ifdef DEBUG
#define debug(x,y) if (x) y
static void lprint(Node, char *);
static void nprint(Node);
static char *rnames(unsigned);
static int id;
static Node lhead;

#else
#define debug(x,y)
#endif

static int rflag;		/* != 0 to trace register allocation */
static int framesize;		/* size of activation record */
static int offset;		/* current frame offset */
static int argbuildsize;	/* size of argument build area */
static int argoffset;		/* offset from top of stack for next argument */
static int nregs = 12;		/* number of allocatable registers */
static unsigned rmask;		/* rmask&(1<<r) == 0 if register r is free */
static unsigned usedmask;	/* usedmask&(1<<r) == 1 if register r was used */
static int reginfo[] = {	/*      1<<x if op+x is legal; */
	0,			/* 0x1000<<x if op+x needs a register */
#include "reginfo.h"
};

static void genreloads(Node, Node, Symbol);
static Symbol genspill(Node);
static void getreg(Node);
static Node *linearize(Node, Node *, Node);
static int needsreg(Node);
static void putreg(Node);
static void ralloc(Node);
static int spillee(Node, unsigned);
static void spill(int, unsigned, Node);
static unsigned uses(Node);
static int valid(int);

#define typecode(p) (optype(p->op) == U ? I : optype(p->op) == B ? P : optype(p->op))
#define sets(p) ((p)->x.rmask<<(p)->x.reg)

/* address - initialize q for addressing expression p+n */
void address(Symbol q, Symbol p, int n) {
	if (p->scope == GLOBAL || p->sclass == STATIC || p->sclass == EXTERN)
		q->x.name = stringf("%s%s%d", p->x.name, n >= 0 ? "+" : "", n);
	else {
		q->x.offset = p->x.offset + n;
		q->x.name = stringf("%d", q->x.offset);
			/* p->scope == PARAM ? "ap" : "fp"); */
	}
}

/* asmcode - emit assembly language specified by asm */
void asmcode(char *str, Symbol argv[]) {
	for ( ; *str; str++)
		if (*str == '%' && str[1] >= 0 && str[1] <= 9)
			print("%s", argv[*++str]->x.name);
		else
			print("%c", *str);
	print("\n");
}

/* blockbeg - begin a compound statement */
void blockbeg(Env *e) {
	assert(rmask == (((~0)<<nregs)|1));
	e->rmask = rmask;
	e->offset = offset;
}

/* blockend - end a compound statement */
void blockend(Env *e) {
	if (offset > framesize)
		framesize = offset;
	offset = e->offset;
	rmask = e->rmask;
}

/* defconst - define a constant */
void defconst(int ty, Value v) {
	switch (ty) {
	case C: print(" .byte %d\n",   v.uc); break;
	case S: print(" .word %d\n",   v.us); break;
	case I: print(" .word $%x,$%x\n",v.i & 0xffff,(v.i>>16)&0xffff); break;
	case U: print(" .word $%x,$%x\n",v.u & 0xffff,(v.u>>16)&0xffff); break;
	case P: print(" .word $%x\n", v.p ); break;
	case F: {
		char buf[MAXLINE];
		sprintf(buf, " .float 0f%.8e\n", v.f);
		outs(buf);
		break;
		}
	case D: {
		char buf[MAXLINE];
		sprintf(buf, " .double 0d%.18e\n", v.d);
		outs(buf);
		break;
		}
	default: assert(0);
	}
}

/* defstring - save
emit a string constant */
void defstring(int len, char *s) {
	while (len-- > 0)
		print(" .byte %d\n", *s++);
}

/* defsymbol - initialize p's Xsymbol fields */
void defsymbol(Symbol p) {
	if (p->scope == CONSTANTS)
		p->x.name = p->name;
	else if (p->scope >= LOCAL && p->sclass == STATIC)
		p->x.name = stringf("L%d", genlabel(1));
	else if (p->generated)
		p->x.name = stringf("L%s", p->name);
	else
		p->x.name = stringf("_%s", p->name);
}

		  /* " FDCSIUPVB" */
#define suffix(p)    ".fdbwllwl."[optype((p)->op)]

/* emit - emit the dags on list p */
void emit(Node p) {
	for (; p; p = p->x.next) {
		Node a = p->kids[0], b = p->kids[1];
		int r = p->x.reg;
		if(p->x.opted!= -1)switch (p->op) {
		case BANDU:				print("\n;and\n");
							print(" LDA r%d\n AND r%d\n STA r%d\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n AND r%d+1\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+1\n AND r%d+2\n STA r%d+1\n ",a->x.reg,b->x.reg,r);
							print(" LDA r%d+1\n AND r%d+3\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							break;
		case BORU:				print("\n;or\n");
							print(" LDA r%d\n ORA r%d\n STA r%d\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n ORA r%d+1\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+1\n ORA r%d+2\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							print(" LDA r%d+1\n ORA r%d+3\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							break;
		case BXORU:				print("\n;xor\n");
							print(" LDA r%d\n EOR r%d\n STA r%d\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n EOR r%d+1\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+1\n EOR r%d+2\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							print(" LDA r%d+1\n EOR r%d+3\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							break;
		case ADDI:  case ADDP:  case ADDU:	print("\n;add\n");
							if(p->x.opted==1)
								{
								print(" CLC\n");
								print(" LDA r%d\n ADC #<[%s]\n STA r%d\n",a->x.reg,p->syms[0]->x.name,r);
								if(suffix(p)=='b')	break;
								print(" LDA r%d+1\n ADC #>[%s]\n STA r%d+1\n",a->x.reg,p->syms[0]->x.name,r);
								if(suffix(p)=='w')	break;
								print(" LDA r%d+2\n ADC #{[%s]\n STA r%d+2\n",a->x.reg,p->syms[0]->x.name,r);
								print(" LDA r%d+3\n ADC #}[%s]\n STA r%d+3\n",a->x.reg,p->syms[0]->x.name,r);
								break;
								}
							print(" CLC\n");
							print(" LDA r%d\n ADC r%d\n STA r%d\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n ADC r%d+1\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+2\n ADC r%d+2\n STA r%d+2\n",a->x.reg,b->x.reg,r);
							print(" LDA r%d+3\n ADC r%d+3\n STA r%d+3\n",a->x.reg,b->x.reg,r);
							break;
		case SUBI:  case SUBP:  case SUBU:	print("\n;sub\n");
							if(p->x.opted==1)
								{
								print(" SEC\n");
								print(" LDA r%d\n SBC #<[%s]\n STA r%d\n",a->x.reg,p->syms[0]->x.name,r);
								if(suffix(p)=='b')	break;
								print(" LDA r%d+1\n SBC #>[%s]\n STA r%d+1\n",a->x.reg,p->syms[0]->x.name,r);
								if(suffix(p)=='w')	break;
								print(" LDA r%d+2\n SBC #{[%s]\n STA r%d+2\n",a->x.reg,p->syms[0]->x.name,r);
								print(" LDA r%d+3\n SBC #}[%s]\n STA r%d+3\n",a->x.reg,p->syms[0]->x.name,r);
								break;
								}
							print(" SEC\n");
							print(" LDA r%d\n SBC r%d\n STA r%d\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n SBC r%d+1\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+1\n SBC r%d+2\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							print(" LDA r%d+1\n SBC r%d+3\n STA r%d+1\n",a->x.reg,b->x.reg,r);
							break;
		case ADDD:  case ADDF:			print(" Fadd");	break;
		case SUBD:  case SUBF:			print(" Fsub");	break;
		case MULD:  case MULF:			print(" mulF");	break;
		case MULI:  case MULU:			print("\n;mul\n");	
							print(" LDA  #0\n STA tmp\n STA tmp+1\n STA tmp+2\n STA tmp+3\n", a->x.reg);
							print(" LDA  r%d\n STA tmp+4  \n", a->x.reg);
							print(" LDA  r%d+1\n STA tmp+5\n", a->x.reg);
							print(" LDA  r%d+2\n STA tmp+6\n", a->x.reg);
							print(" LDA  r%d+3\n STA tmp+7\n", a->x.reg);
							print(" LDA  r%d\n STA tmp2  \n", b->x.reg);
							print(" LDA  r%d+1\n STA tmp2+1\n", b->x.reg);
							print(" LDA  r%d+2\n STA tmp2+2\n", b->x.reg);
							print(" LDA  r%d+3\n STA tmp2+3\n", b->x.reg);
							print(" JSR  mult\n");
							print(" LDA  tmp\n STA r%d  \n", r);
							print(" LDA  tmp+1\n STA r%d+1\n", r);
							print(" LDA  tmp+2\n STA r%d+2\n", r);
							print(" LDA  tmp+3\n STA r%d+3\n", r);
							break;
		case DIVD:  case DIVF:			print(" divF");	break;
		case DIVI:				print("\n;DIVI\n");	
							print(" LDA  r%d\n STA tmp  \n", b->x.reg);
							print(" LDA  r%d+1\n STA tmp+1\n", b->x.reg);
							print(" LDA  r%d+2\n STA tmp+2\n", b->x.reg);
							print(" LDA  r%d+3\n STA tmp+3\n", b->x.reg);
							print(" LDA  r%d\n STA tmp2  \n", a->x.reg);
							print(" LDA  r%d+1\n STA tmp2+1\n", a->x.reg);
							print(" LDA  r%d+2\n STA tmp2+2\n", a->x.reg);
							print(" LDA  r%d+3\n STA tmp2+3\n", a->x.reg);
							print(" JSR  div\n");
							print(" LDA  tmp2\n STA r%d  \n", r);
							print(" LDA  tmp2+1\n STA r%d+1\n", r);
							print(" LDA  tmp2+2\n STA r%d+2\n", r);
							print(" LDA  tmp2+3\n STA r%d+3\n", r);
							break;

		case DIVU:				print("\n;DIVU\n");	
							print(" LDA  r%d\n STA tmp  \n", b->x.reg);
							print(" LDA  r%d+1\n STA tmp+1\n", b->x.reg);
							print(" LDA  r%d+2\n STA tmp+2\n", b->x.reg);
							print(" LDA  r%d+3\n STA tmp+3\n", b->x.reg);
							print(" LDA  r%d\n STA tmp2  \n", a->x.reg);
							print(" LDA  r%d+1\n STA tmp2+1\n", a->x.reg);
							print(" LDA  r%d+2\n STA tmp2+2\n", a->x.reg);
							print(" LDA  r%d+3\n STA tmp2+3\n", a->x.reg);
							print(" JSR  udiv\n");
							print(" LDA  tmp2\n STA r%d  \n", r);
							print(" LDA  tmp2+1\n STA r%d+1\n", r);
							print(" LDA  tmp2+2\n STA r%d+2\n", r);
							print(" LDA  tmp2+3\n STA r%d+3\n", r);
							break;

		case MODI:				print("\n;MODI\n");	
							print(" LDA  r%d\n STA tmp  \n", b->x.reg);
							print(" LDA  r%d+1\n STA tmp+1\n", b->x.reg);
							print(" LDA  r%d+2\n STA tmp+2\n", b->x.reg);
							print(" LDA  r%d+3\n STA tmp+3\n", b->x.reg);
							print(" LDA  r%d\n STA tmp2  \n", a->x.reg);
							print(" LDA  r%d+1\n STA tmp2+1\n", a->x.reg);
							print(" LDA  r%d+2\n STA tmp2+2\n", a->x.reg);
							print(" LDA  r%d+3\n STA tmp2+3\n", a->x.reg);
							print(" JSR  mod\n");
							print(" LDA  tmp1\n STA r%d  \n", r);
							print(" LDA  tmp1+1\n STA r%d+1\n", r);
							print(" LDA  tmp1+2\n STA r%d+2\n", r);
							print(" LDA  tmp1+3\n STA r%d+3\n", r);
							break;

		case MODU:				print("\n;MODU\n");	
							print(" LDA  r%d\n STA tmp  \n", b->x.reg);
							print(" LDA  r%d+1\n STA tmp+1\n", b->x.reg);
							print(" LDA  r%d+2\n STA tmp+2\n", b->x.reg);
							print(" LDA  r%d+3\n STA tmp+3\n", b->x.reg);
							print(" LDA  r%d\n STA tmp2  \n", a->x.reg);
							print(" LDA  r%d+1\n STA tmp2+1\n", a->x.reg);
							print(" LDA  r%d+2\n STA tmp2+2\n", a->x.reg);
							print(" LDA  r%d+3\n STA tmp2+3\n", a->x.reg);
							print(" JSR  udiv\n");
							print(" LDA  tmp1\n STA r%d  \n", r);
							print(" LDA  tmp1+1\n STA r%d+1\n", r);
							print(" LDA  tmp1+2\n STA r%d+2\n", r);
							print(" LDA  tmp1+3\n STA r%d+3\n", r);
							break;

		case RSHU:
			{
			int i;
			i=genlabel(2);
			print("\n;>>\n");
			print(" LDA  r%d\n STA r%d  \n", a->x.reg, r);
			print(" LDA  r%d+1\n STA r%d+1\n", a->x.reg, r);
			print(" LDA  r%d+2\n STA r%d+2\n", a->x.reg, r);
			print(" LDA  r%d+3\n STA r%d+3\n", a->x.reg, r);
			print(" LDX  r%d\n",  b->x.reg);
			print(" BEQ L%d\n",i);
			print("L%d\n",i+1);
			print(" LSR r%d+3\n ROR r%d+2\n ROR r%d+1\n ROR r%d\n", r,r,r,r);
			print(" DEX\n BNE L%d\n",i+1);
			print("L%d\n",i);
			}		
			break;
		case RSHI:
		        {
			int i;
			i=genlabel(2);
			print("\n;>>???\n");
			print(" LDA  r%d\n STA r%d  \n", a->x.reg, r);
			print(" LDA  r%d+1\n STA r%d+1\n", a->x.reg, r);
			print(" LDA  r%d+2\n STA r%d+2\n", a->x.reg, r);
			print(" LDA  r%d+3\n STA r%d+3\n", a->x.reg, r);
			print(" ASL A\n LDA #0\n ROL A\n ROL A\n TAY\n");/* sign extend stuff */
			print(" LDX  r%d\n",  b->x.reg);
			print(" BEQ L%d\n",i);
			print("L%d\n",i+1);
			print(" CPY #1\n"); /* sign extend stuff */
			print(" LSR r%d+3\n ROR r%d+2\n ROR r%d+1\n ROR r%d\n", r,r,r,r);
			print(" DEX\n BNE L%d\n",i+1);
			print("L%d\n",i);
			}
			break;
		
		case LSHI: case LSHU:
		        {
			int i;
			i=genlabel(2);
			print("\n;<<\n");
			print(" LDA r%d\n STA r%d  \n", a->x.reg, r);
			print(" LDA r%d+1\n STA r%d+1\n", a->x.reg, r);
			print(" LDA r%d+2\n STA r%d+2\n", a->x.reg, r);
			print(" LDA r%d+3\n STA r%d+3\n", a->x.reg, r);
			print(" LDX r%d\n",  b->x.reg);
			print(" BEQ L%d\n",i);
			print("L%d\n",i+1);
			print(" ASL r%d\n ROL r%d+1\n ROL r%d+2\n ROL r%d+3\n", r,r,r,r);
			print(" DEX\n BNE L%d\n",i+1);
			print("L%d\n",i);
			}
			break;
		case INDIRB:
			if(p->x.opted==0)
			    {
			    print("\n;loadB?\n");
			    print(";moval (r%d),r%d\n", a->x.reg, r);
			    print(" LDA r%d\n STA r%d\n",a->x.reg, r);
			    print(" LDA r%d+1\n STA r%d+1\n",a->x.reg, r);
			    break;
			    }
			if(p->x.opted==1)
			    {
			    print("\n;loadParam\n");
			    print(" CLC\n");
			    print(" LDA ap  \n ADC #<[%s]\n STA r%d\n"  ,p->syms[0]->x.name, r);
			    print(" LDA ap+1\n ADC #>[%s]\n STA r%d+1\n",p->syms[0]->x.name, r);
			    break;
			    }
			if(p->x.opted==2)
			    {
			    print("\n;loadGlobal\n");
			    print(" LDA #<[%s]\n STA r%d\n"  ,p->syms[0]->x.name, r);
			    print(" LDA #>[%s]\n STA r%d+1\n",p->syms[0]->x.name, r);
			    break;
			    }
		case INDIRC: case INDIRD: case INDIRF: case INDIRI:
		case INDIRP: case INDIRS:
			if(p->x.opted==0)
			    {
			    print("\n;load\n");
			    print(" LDY #0\n LDA (r%d),Y\n STA r%d\n",a->x.reg, r);
			    if(suffix(p)=='b')	break;
			    print(" INY\n LDA (r%d),Y\n STA r%d+1\n",a->x.reg, r);
			    if(suffix(p)=='w')	break;
			    print(" INY\n LDA (r%d),Y\n STA r%d+2\n",a->x.reg, r);
			    print(" INY\n LDA (r%d),Y\n STA r%d+3\n",a->x.reg, r);
			    break;
			    }
			if(p->x.opted==1)
			    {
			    print("\n;loadParam\n");
			    print(" LDY #%s\n" ,p->syms[0]->x.name);
			    print(" LDA (ap),Y\n STA r%d\n", r);
			    if(suffix(p)=='b')	break;
			    print(" INY\n LDA (ap),Y\n STA r%d+1\n", r);
			    if(suffix(p)=='w')	break;
			    print(" INY\n LDA (ap),Y\n STA r%d+2\n", r);
			    print(" INY\n LDA (ap),Y\n STA r%d+3\n", r);
			    break;
			    }
			if(p->x.opted==2)
			    {
			    print("\n;loadGlobal\n");
			    print(" LDA %s\n STA r%d\n" ,p->syms[0]->x.name, r);
			    if(suffix(p)=='b')	break;
			    print(" LDA %s+1\n STA r%d+1\n" ,p->syms[0]->x.name, r);
			    if(suffix(p)=='w')	break;
			    print(" LDA %s+2\n STA r%d+2\n" ,p->syms[0]->x.name, r);
			    print(" LDA %s+3\n STA r%d+3\n" ,p->syms[0]->x.name, r);
			    break;
			    }
		case BCOMU:				print("\n;com\n");
							print(" LDA r%d\n EOR #$FF\n STA r%d\n",a->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n EOR #$FF\n STA r%d+1\n",a->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+2\n EOR #$FF\n STA r%d+2\n",a->x.reg,r);
							print(" LDA r%d+3\n EOR #$FF\n STA r%d+3\n",a->x.reg,r);
							break;
		case NEGI:    				print("\n;neg\n");
							print(" SEC");
							print(" LDA r%d\n EOR #$FF\n ADC #0\n STA r%d\n",a->x.reg,r);
							if(suffix(p)=='b')	break;
							print(" LDA r%d+1\n EOR #$FF\n ADC #0\n STA r%d+1\n",a->x.reg,r);
							if(suffix(p)=='w')	break;
							print(" LDA r%d+2\n EOR #$FF\n ADC #0\n STA r%d+2\n",a->x.reg,r);
							print(" LDA r%d+3\n EOR #$FF\n ADC #0\n STA r%d+3\n",a->x.reg,r);
							break;
	
		
		case CVCI:				{
							int i;
							i=genlabel(1);
							print("\n;CVCI\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" ASL A\n LDA #$FF\n BCS L%d\n",i);
							print(" LDA #0\n");
							print("L%d\n",i);
							print(" STA r%d+1\n",r);
							print(" STA r%d+2\n",r);
							print(" STA r%d+3\n",r);
							}
							break;

		case CVCU:			   	print("\n; CVCU\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" LDA #0\n");
							print(" STA r%d+1\n",r);
							print(" STA r%d+2\n",r);
							print(" STA r%d+3\n",r);
							break;
		case CVSI:				{
							int i;
							i=genlabel(1);
							print("\n;CVCI\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" LDA r%d+1\n STA r%d+1\n",a->x.reg,r);
							print(" ASL A\n LDA #$FF\n BCS L%d\n",i);
							print(" LDA #0\n");
							print("L%d\n",i);
							print(" STA r%d+2\n",r);
							print(" STA r%d+3\n",r);
							}
							break;
		case CVSU:case CVPU:			print("\n;CVSU\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" LDA r%d+1\n STA r%d+1\n",a->x.reg,r);
							print(" LDA #0\n");
							print(" STA r%d+2\n",r);
							print(" STA r%d+3\n",r);
							break;
		case CVUC: case CVIC:			print("\n;CVUC\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							break;
		case CVUS: case CVIS:case CVUP: 	print("\n;CVUS\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" LDA r%d+1\n STA r%d+1\n",a->x.reg,r);
							break;
		case CVIU:  case CVUI: 			print("\n;CVIU/UI\n");
							print(" LDA r%d\n STA r%d\n",a->x.reg,r);
							print(" LDA r%d+1\n STA r%d+1\n",a->x.reg,r);
							print(" LDA r%d+2\n STA r%d+2\n",a->x.reg,r);
							print(" LDA r%d+3\n STA r%d+3\n",a->x.reg,r);
							break;
		case CVID:				print(" cvtl" );
							break;
 		case CVDF:  case CVDI:			print(" cvtd" );
							break;
		case CVFD:				print(" cvtf" );
							break;
		case NEGD:  case NEGF:  		print(" mneg" );
							break;
		
		case RETD: case RETF: case RETI:
			print(";return\n");
			if(p->x.opted==0)
			    {
			    print(" LDA r%d\n STA r0\n",a->x.reg);
			    if(suffix(p)=='b')	break;
			    print(" LDA r%d+1\n STA r0+1\n",a->x.reg);
			    if(suffix(p)=='w')	break;
			    print(" LDA r%d+2\n STA r0+2\n",a->x.reg);
			    print(" LDA r%d+3\n STA r0+3\n",a->x.reg);
			    break;
			    }
			else
			    {
			    print(" LDA #<[%s]\n STA r0\n",p->syms[0]->x.name);
			    if(suffix(p)=='b')	break;
			    print(" LDA #>[%s]\n STA r0+1\n",p->syms[0]->x.name);
			    if(suffix(p)=='w')	break;
			    print(" LDA #{[%s]\n STA r0+2\n",p->syms[0]->x.name);
			    print(" LDA #}[%s]\n STA r0+3\n",p->syms[0]->x.name);
			    break;
			    }
		case RETV:
			print(";return\n");
			break;
			
		case ADDRGP:
			if(p->x.opted== -1) break;
 			print("\n;ADDRGP\n");
			print(" LDA #<[%s]\n STA r%d\n"  ,p->syms[0]->x.name, r);
			print(" LDA #>[%s]\n STA r%d+1\n",p->syms[0]->x.name, r);
			break;
		case ADDRFP:
			if(p->x.opted== -1) break;
			print("\n;ADDRFP??\n");
			print(" CLC\n");
			print(" LDA ap  \n ADC #<[%s]\n STA r%d\n"  ,p->syms[0]->x.name, r);
			print(" LDA ap+1\n ADC #>[%s]\n STA r%d+1\n",p->syms[0]->x.name, r);
			break;
		case ADDRLP:
			if(p->x.opted== -1) break;
			print("\n;ADDRLP\n");
			print(" CLC\n");
			print(" LDA fp  \n ADC #<[%s]\n STA r%d\n"  ,p->syms[0]->x.name, r);
			print(" LDA fp+1\n ADC #>[%s]\n STA r%d+1\n",p->syms[0]->x.name, r);
			break;
			
		case CNSTC: case CNSTI: case CNSTP:
		case CNSTS: case CNSTU:
			if(p->x.opted== -1) break;
			print("\n;constant\n");
			print(" LDA #<[%s] \n STA r%d  \n",p->syms[0]->x.name, r);
			if(suffix(p)=='b')	break;
			print(" LDA #>[%s] \n STA r%d+1\n",p->syms[0]->x.name, r);
			if(suffix(p)=='w')	break;
			print(" LDA #{[%s]\n STA r%d+2\n",p->syms[0]->x.name, r);
			print(" LDA #}[%s]\n STA r%d+3\n",p->syms[0]->x.name, r);
			break;
		case JUMPV:
			if(p->x.opted==0)
				print(" JMP (r%d)\n", a->x.reg);
			else
				print(" JMP %s\n", p->syms[0]->x.name);
			break;
		case ASGNB:
			{
			int i;
			i=genlabel(1);
			print("\n;storeB\n");
			print(";movc3 $%s,(r%d),(r%d)\n", p->syms[0]->x.name,
				b->x.reg, a->x.reg);
			print(" LDY #$%s\n",p->syms[0]->x.name);
			print("L%d\n DEY\n ",i);
			print(" LDA (r%d),Y\n STA (r%d),Y\n",b->x.reg, a->x.reg);
			print(" CPY #0\n BNE L%d\n",i);
			break;
			}
		case ASGNC: case ASGND: case ASGNF: case ASGNI: case ASGNP: case ASGNS:
			{
			if(p->x.opted==0)
			    {
			    print("\n;store\n");
			    print(" LDY #0\n LDA r%d\n STA (r%d),Y\n",b->x.reg, a->x.reg);
			    if(suffix(p)=='b')	break;
			    print(" INY\n LDA r%d+1\n STA (r%d),Y\n",b->x.reg, a->x.reg);
			    if(suffix(p)=='w')	break;
			    print(" INY\n LDA r%d+2\n STA (r%d),Y\n",b->x.reg, a->x.reg);
			    print(" INY\n LDA r%d+3\n STA (r%d),Y\n",b->x.reg, a->x.reg);
			    break;
			    }
			if(p->x.opted==2)
			    {
			    print("\n;storeGlobal\n");
			    print(" LDA r%d\n STA %s\n",b->x.reg, p->syms[0]->x.name);
			    if(suffix(p)=='b')	break;
			    print(" LDA r%d+1\n STA %s+1\n",b->x.reg, p->syms[0]->x.name);
			    if(suffix(p)=='w')	break;
			    print(" LDA r%d+2\n STA %s+2\n",b->x.reg, p->syms[0]->x.name);
			    print(" LDA r%d+3\n STA %s+3\n",b->x.reg, p->syms[0]->x.name);
			    break;
			    }
			if(p->x.opted==3)
			    {
			    print("\n;storeConstant\n");
			    print(" LDY #0\n");
			    print(" LDA #<[%s]\n STA (r%d),Y\n",p->syms[1]->x.name, a->x.reg);
			    if(suffix(p)=='b')	break;
			    print(" INY\n LDA #>[%s]\n STA (r%d),Y\n",p->syms[1]->x.name, a->x.reg);
			    if(suffix(p)=='w')	break;
			    print(" INY\n LDA #{[%s]\n STA (r%d),Y\n",p->syms[1]->x.name, a->x.reg);
			    print(" INY\n LDA #}[%s]\n STA (r%d),Y\n",p->syms[1]->x.name, a->x.reg);
			    break;
			    }
			if(p->x.opted==4)
			    {
			    print("\n;storeConstantGlobal\n");
			    print(" LDA #<[%s]\n STA %s\n",p->syms[1]->x.name, p->syms[0]->x.name);
			    if(suffix(p)=='b')	break;
			    print(" INY\n LDA #>[%s]\n STA %s+1\n",p->syms[1]->x.name, p->syms[0]->x.name);
			    if(suffix(p)=='w')	break;
			    print(" INY\n LDA #{[%s]\n STA %s+2\n",p->syms[1]->x.name, p->syms[0]->x.name);
			    print(" INY\n LDA #}[%s]\n STA %s+3\n",p->syms[1]->x.name, p->syms[0]->x.name);
			    break;
			    }
			print(" ERROR");
			break;
			}
		case ARGB:
			print(";movc3 $%s,(r%d),%d(sp)\n", p->syms[0]->x.name,
				a->x.reg, p->x.argoffset);
			print("\n;stack;\n");
			{
			int i;
			
			print(" CLC\n LDA sp  \n ADC #%d\n STA tmp  \n",p->x.argoffset & 0xff);
			print(" LDA sp+1\n ADC #%d\n STA tmp+1\n",((p->x.argoffset)>>8) & 0xff);
			
			i=genlabel(1);
			print(" LDY #$%s\n",p->syms[0]->x.name);
			print("L%d\n DEY\n ",i);
			print(" LDA (r%d),Y\n STA (tmp),Y\n",a->x.reg);
			print(" CPY #0\n BNE L%d\n",i);
			break;
			}
			break;
		case ARGD: case ARGF: case ARGI: case ARGP:
			print("\n;stack;\n");
			if(p->x.argoffset >252)
			    {
			    if(p->x.opted==0)
				{
				print(" CLC\n LDA sp  \n ADC #%d\n STA tmp  \n",p->x.argoffset & 0xff);
				print(" LDA sp+1\n ADC #%d\n STA tmp+1\n",((p->x.argoffset)>>8) & 0xff);
				print(" LDY #0\n LDA r%d  \n STA (tmp),Y\n",a->x.reg);
				if(suffix(p)=='b')	break;
				print(" INY  \n LDA r%d+1\n STA (tmp),Y\n",a->x.reg);
				if(suffix(p)=='w')	break;
				print(" INY  \n LDA r%d+2\n STA (tmp),Y\n",a->x.reg);
				print(" INY  \n LDA r%d+3\n STA (tmp),Y\n",a->x.reg);
				}
			    else
			    	{
				print(" CLC\n LDA sp  \n ADC #%d\n STA tmp  \n",p->x.argoffset & 0xff);
				print(" LDA sp+1\n ADC #%d\n STA tmp+1\n",((p->x.argoffset)>>8) & 0xff);
				print(" LDY #0\n LDA #<[%s] \n STA (tmp),Y\n",p->syms[1]->x.name);
				if(suffix(p)=='b')	break;
				print(" INY  \n LDA #>[%s]\n STA (tmp),Y\n",p->syms[1]->x.name);
				if(suffix(p)=='w')	break;
				print(" INY  \n LDA #{[%s]\n STA (tmp),Y\n",p->syms[1]->x.name);
				print(" INY  \n LDA #}[%s]\n STA (tmp),Y\n",p->syms[1]->x.name);
				}
			    }
			else
			    {
			    if(p->x.opted==0)
				{
				print(" LDY #%d\n",p->x.argoffset);
				print(" LDA r%d  \n STA (sp),Y\n",a->x.reg);
				if(suffix(p)=='b')	break;
				print(" INY  \n LDA r%d+1\n STA (sp),Y\n",a->x.reg);
				if(suffix(p)=='w')	break;
				print(" INY  \n LDA r%d+2\n STA (sp),Y\n",a->x.reg);
				print(" INY  \n LDA r%d+3\n STA (sp),Y\n",a->x.reg);
				}
			    else
				{
				print(" LDY #%d\n",p->x.argoffset);
				print(" LDA #<[%s] \n STA (sp),Y\n",p->syms[1]->x.name);
				if(suffix(p)=='b')	break;
				print(" INY  \n LDA #>[%s]\n STA (sp),Y\n",p->syms[1]->x.name);
				if(suffix(p)=='w')	break;
				print(" INY  \n LDA #{[%s]\n STA (sp),Y\n",p->syms[1]->x.name);
				print(" INY  \n LDA #}[%s]\n STA (sp),Y\n",p->syms[1]->x.name);

				}
			    }
			break;
		case CALLB:
			{
			int i;
			i=genlabel(1);
			if (p->x.opted==0 && a->x.reg == 1)
				{
				print(" LDA r1  \n STA r0  \n");
				print(" LDA r1+1\n STA r0+1\n");
				print(" LDA r1+2\n STA r0+2\n");
				print(" LDA r1+3\n STA r0+3\n");

				a->x.reg = 0;
				}
			if (b->x.reg != 1)
				{
				print(" LDA r%d  \n STA r1  \n", b->x.reg);
				print(" LDA r%d+1\n STA r1+1\n", b->x.reg);
				print(" LDA r%d+2\n STA r1+2\n", b->x.reg);
				print(" LDA r%d+3\n STA r1+3\n", b->x.reg);
				}
			if(p->x.opted==0)
			    {
			    print(" LDA #>L%d\n PHA\n",i);
			    print(" LDA #<L%d\n PHA\n",i);
			    print(" JMP (r%d)\n", a->x.reg);
			    print("L%d NOP\n",i);
			    break;
			    }
			if(p->x.opted==1)
			    print(" JSR %s\n",p->syms[0]->x.name);
			break;
			}
		case CALLD: case CALLF: case CALLI: case CALLV:
			{
			int i;
			i=genlabel(1);
			print("\n;calling\n");
			if(p->x.opted==0)
			    {
			    print(" LDA #>L%d\n PHA\n",i);
			    print(" LDA #<L%d\n PHA\n",i);
			    print(" JMP (r%d)\n", a->x.reg);
			    print("L%d NOP\n",i);
			    }
			if(p->x.opted==1)
			    print(" JSR %s\n",p->syms[0]->x.name);
			if (p->op != CALLV)
				{
				print(" LDA r0  \n STA r%d\n",r);
				if(suffix(p)=='b')	break;
				print(" LDA r0+1\n STA r%d+1\n",r);
				if(suffix(p)=='w')	break;
				print(" LDA r0+2\n STA r%d+2\n",r);
				print(" LDA r0+3\n STA r%d+3\n",r);
				}
			break;
			}
		case EQD:   case EQF:   case EQI:	{
							int i;
							i=genlabel(1);
							if(p->x.opted==0)
							    {
							    print("\n;==\n");
							    print(" LDA r%d\n CMP r%d\n BNE L%d\n",a->x.reg,b->x.reg,i);
							    print(" LDA r%d+1\n CMP r%d+1\n BNE L%d\n",a->x.reg,b->x.reg,i);
							    print(" LDA r%d+2\n CMP r%d+2\n BNE L%d\n",a->x.reg,b->x.reg,i);
							    print(" LDA r%d+3\n CMP r%d+3\n BNE L%d\n",a->x.reg,b->x.reg,i);
							    print(" JMP %s\n",p->syms[0]->x.name);
							    print("L%d\n",i);
							    }
							if(p->x.opted==1)
							    {
							    print("\n;==K\n");
							    print(" LDA r%d\n CMP #<[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+1\n CMP #>[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+2\n CMP #{[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+3\n CMP #}[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" JMP %s\n",p->syms[0]->x.name);
							    print("L%d\n",i);
							    }
							print("\n;==ERROR\n");
							break;
							}
		case GEI:				{
							int i;
							i=genlabel(1);
							print("\n;>=\n");
							print(" LDA r%d\n EOR #$80\n STA tmp\n",a->x.reg);
							print(" LDA r%d\n EOR #$80\n STA tmp+1\n",b->x.reg);
							print(" LDA r%d\n CMP r%d  \n",a->x.reg,b->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",a->x.reg,b->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",a->x.reg,b->x.reg);
							print(" LDA tmp\n SBC tmp+1\n");
							print(" BCC L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case GEU:				{
							int i;
							i=genlabel(1);
							print("\n;>=\n");
							print(" LDA r%d\n CMP r%d  \n",a->x.reg,b->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",a->x.reg,b->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",a->x.reg,b->x.reg);
							print(" LDA r%d+3\n SBC r%d+3\n",a->x.reg,b->x.reg);
							print(" BCC L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case GTI:				{
							int i;
							i=genlabel(1);
							print("\n;>\n");
							print(" LDA r%d\n EOR #$80\n STA tmp\n",a->x.reg);
							print(" LDA r%d\n EOR #$80\n STA tmp+1\n",b->x.reg);
							print(" LDA r%d\n CMP r%d  \n",b->x.reg,a->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",b->x.reg,a->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",b->x.reg,a->x.reg);
							print(" LDA tmp+1\n SBC tmp\n");
							print(" BCS L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case GTU:				{
							int i;
							i=genlabel(1);
							print("\n;<\n");
							print(" LDA r%d\n CMP r%d  \n",b->x.reg,a->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",b->x.reg,a->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",b->x.reg,a->x.reg);
							print(" LDA r%d+3\n SBC r%d+3\n",b->x.reg,a->x.reg);
							print(" BCS L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}

		case LEI:				{
							int i;
							i=genlabel(1);
							print("\n;<\n");
							print(" LDA r%d\n EOR #$80\n STA tmp\n",a->x.reg);
							print(" LDA r%d\n EOR #$80\n STA tmp+1\n",b->x.reg);
							print(" LDA r%d\n CMP r%d  \n",b->x.reg,a->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",b->x.reg,a->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",b->x.reg,a->x.reg);
							print(" LDA tmp+1\n SBC tmp\n");
							print(" BCC L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case LEU:				{
							int i;
							i=genlabel(1);
							print("\n;<\n");
							print(" LDA r%d\n CMP r%d  \n",b->x.reg,a->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",b->x.reg,a->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",b->x.reg,a->x.reg);
							print(" LDA r%d+3\n SBC r%d+3\n",b->x.reg,a->x.reg);
							print(" BCC L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case LTI:				{
							int i;
							i=genlabel(1);
							print("\n;<\n");
							print(" LDA r%d\n EOR #$80\n STA tmp\n",a->x.reg);
							print(" LDA r%d\n EOR #$80\n STA tmp+1\n",b->x.reg);
							print(" LDA r%d\n CMP r%d\n",a->x.reg,b->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",a->x.reg,b->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",a->x.reg,b->x.reg);
							print(" LDA tmp\n SBC tmp+1\n");
							print(" BCS L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case LTU:				{
							int i;
							i=genlabel(1);
							print("\n;<\n");
							print(" LDA r%d\n CMP r%d\n",a->x.reg,b->x.reg);
							print(" LDA r%d+1\n SBC r%d+1\n",a->x.reg,b->x.reg);
							print(" LDA r%d+2\n SBC r%d+2\n",a->x.reg,b->x.reg);
							print(" LDA r%d+3\n SBC r%d+3\n",a->x.reg,b->x.reg);
							print(" BCS L%d\n",i);
							print(" JMP %s\n",p->syms[0]->x.name);
							print("L%d\n",i);
							break;
							}
		case NED:   case NEF:   case NEI:	if(p->x.opted==0)
							{
							int i;
							i=genlabel(2);
							print("\n;neq\n");
							print(" LDA r%d\n CMP r%d\n BNE L%d\n",a->x.reg,b->x.reg,i);
							print(" LDA r%d+1\n CMP r%d+1\n BNE L%d\n",a->x.reg,b->x.reg,i);
							print(" LDA r%d+2\n CMP r%d+2\n BNE L%d\n",a->x.reg,b->x.reg,i);
							print(" LDA r%d+3\n CMP r%d+3\n  BNE L%d\n",a->x.reg,b->x.reg,i);
							print(" JMP L%d\n",i+1);
							print("L%d\n",i);
							print(" JMP %s;\n",p->syms[0]->x.name);
							print("L%d\n",i+1);
							break;
							}
							if(p->x.opted==1)
							    {
							    int i;
							    i=genlabel(2);
							    print("\n;!=K\n");
							    print(" LDA r%d\n CMP #<[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+1\n CMP #>[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+2\n CMP #{[%s]\n BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" LDA r%d+3\n CMP #}[%s]\n  BNE L%d\n",a->x.reg,p->syms[1]->x.name,i);
							    print(" JMP L%d\n",i+1);
							    print("L%d\n",i);
							    print(" JMP %s;\n",p->syms[0]->x.name);
							    print("L%d\n",i+1);
							    break;
							    }
		case GED:   case GEF:
		case GTD:   case GTF:
		case LED:   case LEF:
		case LTD:   case LTF:			print("\n Float Compare - not Supported\n");
							break;
		case LABELV:
			print("%s\n", p->syms[0]->x.name);
			break;
		default: assert(0);
		}
	}
}

/* function - generate code for a function */
void function(Symbol f, Symbol caller[],
	Symbol callee[], int ncalls) {
	int i,j;

	offset = 4;
	for (i = 0; caller[i] && callee[i]; i++) {
		offset = roundup(offset, caller[i]->type->align);
		callee[i]->x.offset = caller[i]->x.offset = offset;
		callee[i]->x.name = caller[i]->x.name = stringf("%d", offset);
		offset += caller[i]->type->size;
		callee[i]->sclass = AUTO;
	}
	usedmask = argbuildsize = framesize = offset = 0;
	gencode(caller, callee);
	print("%s\n", f->x.name);
	framesize += argbuildsize;
	j=0;
	for(i=0;i<nregs;i++)
		if((usedmask&~0x1) & 1<<i)
			j++;
	print(" SEC\n");
	print(" LDA sp\n SBC #<%d\n STA tmp\n",j*4+2*2+4/*allign*/);
	print(" LDA sp+1\n SBC #>%d\n STA tmp+1\n",j*4+2*2+4/*allign*/);
	
	print(" LDY #0\n LDA ap\n STA (tmp),Y\n");
	print(" INY   \n LDA ap+1\n STA (tmp),Y\n");
	print(" INY   \n LDA fp\n STA (tmp),Y\n");
	print(" INY   \n LDA fp+1\n STA (tmp),Y\n");
	
	print(" ;rmask= $%x\n", usedmask&~0x1);
	for(i=0;i<nregs;i++)
		if((usedmask&~0x1) & 1<<i)
			{
			print(" INY   \n LDA r%d\n STA (tmp),Y\n",i);
			print(" INY   \n LDA r%d+1\n STA (tmp),Y\n",i);
			print(" INY   \n LDA r%d+2\n STA (tmp),Y\n",i);
			print(" INY   \n LDA r%d+3\n STA (tmp),Y\n",i);
			};
			
	print(" SEC\n LDA sp\n SBC #4\n STA ap\n");
	print(" LDA sp+1\n SBC #0\n STA ap+1\n");
	
	print(" SEC\n LDA tmp\n STA fp  \n SBC #<%d\n STA sp  \n", framesize);
	print(" LDA tmp+1    \n STA fp+1\n SBC #>%d\n STA sp+1\n", framesize);
	
	if (isstruct(freturn(f->type)))
		print("movl r1,-4(fp)\n");
	emitcode();
	print(" CLC\n LDA fp  \n STA tmp  \n ADC #<%d\n STA sp\n", j*4+2*2+4/*allign*/);
	print(" LDA fp+1\n STA tmp+1\n ADC #>%d\n STA sp+1\n\n", j*4+2*2+4/*allign*/);
			
	print(" LDY #0\n LDA (tmp),Y\n STA ap\n");
	print(" INY   \n LDA (tmp),Y\n STA ap+1\n");
	print(" INY   \n LDA (tmp),Y\n STA fp\n");
	print(" INY   \n LDA (tmp),Y\n STA fp+1\n");
	
	for(i=0;i<nregs;i++)
		if((usedmask&~0x1) & 1<<i)
			{
			print(" INY   \n LDA (tmp),Y\n STA r%d\n",i);
			print(" INY   \n LDA (tmp),Y\n STA r%d+1\n",i);
			print(" INY   \n LDA (tmp),Y\n STA r%d+2\n",i);
			print(" INY   \n LDA (tmp),Y\n STA r%d+3\n",i);
			};
				
	print(" RTS\n");
}
Node xoptim(Node p)
{
if(p->x.opted!=0) return p;

if(p->kids[0]!=NULL) p->kids[0]= xoptim(p->kids[0]);
if(p->kids[1]!=NULL) p->kids[1]= xoptim(p->kids[1]);

if((generic(p->op) == CALL || generic(p->op) == JUMP) && generic(p->kids[0]->op)==ADDRG)
	{
	fputs("Call/Jump Global\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->x.opted=1;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}

if(generic(p->op) == INDIR && generic(p->kids[0]->op)==ADDRG)
	{
	fputs("Load Global\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->x.opted=2;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}
if(generic(p->op) == ASGN && generic(p->kids[0]->op)==ADDRG && generic(p->kids[1]->op)==CNST)
	{
	if(p->op== ASGNB) {p->x.opted==0; return p;};
	fputs("Save Constant Global\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->syms[1]=p->kids[1]->syms[0];
	p->x.opted=4;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	if(--(p->kids[1]->count)==0);
	    p->kids[1]->x.opted= -1;
	p->kids[1]=NULL;
	return p;
	}

if(generic(p->op) == ASGN && generic(p->kids[0]->op)==ADDRG)
	{
	if(p->op== ASGNB) {p->x.opted==0; return p;};
	fputs("Save Global\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->x.opted=2;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}
if(generic(p->op) == ASGN && generic(p->kids[1]->op)==CNST)
	{
	if(p->op== ASGNB) {p->x.opted==0; return p;};
	fputs("Save Constant\n", stderr);
	p->syms[1]=p->kids[1]->syms[0];
	p->x.opted=3;
	
	if(--(p->kids[1]->count)==0);
	    p->kids[1]->x.opted= -1;
	p->kids[1]=NULL;
	return p;
	}
if(generic(p->op) == ARG && generic(p->kids[0]->op)==CNST)
	{
	if(p->op== ARGB) {p->x.opted==0; return p;};
	fputs("Stack Constant\n", stderr);
	p->syms[1]=p->kids[0]->syms[0];
	p->x.opted=1;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}

if(generic(p->op) == RET && p->op != RETV && generic(p->kids[0]->op)==CNST)
	{
	fputs("Ret Constant\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->x.opted=1;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}

if(generic(p->op) == INDIR && generic(p->kids[0]->op)==ADDRF)
	{
	fputs("Load Param\n", stderr);
	p->syms[0]=p->kids[0]->syms[0];
	p->x.opted=1;
	
	if(--(p->kids[0]->count)==0);
	    p->kids[0]->x.opted= -1;
	p->kids[0]=NULL;
	return p;
	}

if((generic(p->op) == ADD || generic(p->op) == EQ || generic(p->op) == NE) && generic(p->kids[0]->op)==CNST)
	{
	Node tmp;
	tmp=p->kids[0];
	p->kids[0]=p->kids[1];
	p->kids[1]=tmp;
	}
if((generic(p->op) == ADD || generic(p->op) == SUB) && p->kids[1] && generic(p->kids[1]->op)==CNST)
	{
	fputs("Add/Sub Constant\n", stderr);
	p->syms[0]=p->kids[1]->syms[0];
	p->x.opted=1;
	if(--(p->kids[1]->count)==0);
	    p->kids[1]->x.opted= -1;
	p->kids[1]=NULL;
	return p;
	}
if((generic(p->op) == EQ || generic(p->op) == NE) && p->kids[1] && generic(p->kids[1]->op)==CNST)
	{
	fputs("EQ/NE Constant\n", stderr);
	p->syms[1]=p->kids[1]->syms[0];
	p->x.opted=1;
	if(--(p->kids[1]->count)==0);
	    p->kids[1]->x.opted= -1;
	p->kids[1]=NULL;
	return p;
	}
p->x.opted=0;
return p;
}

Node xmark(Node p)
{
if (p==NULL)
return p;
p->x.opted=0;
xmark(p->kids[0]);
xmark(p->kids[1]);
return p;
}

/* gen - generate code for the dags on list p */
Node gen(Node p) {
	Node head, *last;

	debug(1,id = 0);

	head=p;
	for (; p; p = p->link)
		p=xmark(p);
	p=head;
	
	head=p;
	for (; p; p = p->link)
		p=xoptim(p);
	p=head;
	
	for (last = &head; p; p = p->link)
		last = linearize(p, last, 0);
	debug(rflag,(lhead = head, lprint(head," before ralloc")));
	for (p = head; p; p = p->x.next) {
		ralloc(p);
		if (p->count == 0 && sets(p))
			putreg(p);
	}
	debug(rflag,lprint(lhead," after ralloc"));
	return head;
}

/* getreg - allocate 1 or 2 registers for node p */
static void getreg(Node p) {
	int r, m = optype(p->op) == D ? 3 : 1;

	for (r = 0; r < nregs; r++)
		if ((rmask&(m<<r)) == 0) {
			p->x.rmask = m;
			p->x.reg = r;
			rmask |= sets(p);
			usedmask |= sets(p);
			debug(rflag,fprint(2,"allocating %s to node #%d\n", rnames(sets(p)), p->x.id));
			return;
		}
	debug(rflag,lprint(lhead, " before spillee"));
	r = spillee(p, m);
	spill(r, m, p);
	debug(rflag,lprint(lhead, " after spill"));
	assert((rmask&(m<<r)) == 0);
	getreg(p);
}

/* genreloads - make the nodes after dot use reloads of temp instead of p's register */
static void genreloads(Node dot, Node p, Symbol temp) {
	int i;
	Node last;

	for (last = dot; dot = dot->x.next; last = dot)
		for (i = 0; i < MAXKIDS; i++)
			if (dot->kids[i] == p) {
				dot->kids[i] = newnode(INDIR + typecode(p),
					newnode(ADDRL+P, 0, 0, temp), 0, 0);
				dot->kids[i]->count = 1;
				p->count--;
				linearize(dot->kids[i], &last->x.next, last->x.next);
				last = dot->kids[i];
			}
	assert(p->count == 0);
}

/* genspill - generate code to spill p's register and return the temporary used */
static Symbol genspill(Node p) {
	Symbol temp = newtemp(AUTO, typecode(p));
	Node q = p->x.next;

	linearize(newnode(ASGN + typecode(p),
		newnode(ADDRLP, 0, 0, temp), p, 0),
		&p->x.next, p->x.next);
	rmask &= ~1;
	for (p = p->x.next; p != q; p = p->x.next)
		ralloc(p);
	rmask |= 1;
	return temp;
}

/* global - global id */
void global(Symbol p) {
	switch (p->type->align) {
	case 2: print(".align 1; "); break;
	case 4: print(".align 2; "); break;
	case 8: print(".align 3; "); break;
	}
	print("%s", p->x.name);
}

/* linearize - linearize node list p */
static Node *linearize(Node p, Node *last, Node next) {
	if (p && !p->x.visited) {
		last = linearize(p->kids[0], last, 0);
		last = linearize(p->kids[1], last, 0);
		p->x.visited = 1;
		*last = p;
		last = &p->x.next;
		debug(1,if (p->x.id == 0) p->x.id = ++id);		
		debug(rflag,{fprint(2,"listing node "); nprint(p);})
	}
	*last = next;
	return last;
}

/* local - local variable */
void local(Symbol p) {
	offset = roundup(offset + p->type->size, p->type->align);
	offset = roundup(offset, 4);
	p->x.offset = -offset;
	p->x.name = stringf("$%x", 0x10000-offset); /*(fp) */
	p->sclass = AUTO;
}

/* needsreg - does p need a register? */
static int needsreg(p) Node p; {
	assert(opindex(p->op) > 0 && opindex(p->op) < sizeof reginfo/sizeof reginfo[0]);
	return reginfo[opindex(p->op)]&(0x1000<<optype(p->op));
}

/* progbeg - beginning of program */
void progbeg(int argc, char *argv[]) {
	extern int atoi(char *);		/* (omit) */
	while (--argc > 0)
		if (**++argv == '-' && argv[0][1] >= '0' && argv[0][1] <= '9')
			nregs = atoi(*argv + 1);
		else if (strcmp(*argv, "-r") == 0)	/* (omit) */
			rflag++;			/* (omit) */
	rmask = ((~0)<<nregs)|1;
}

/* putreg - decrement register usage */
static void putreg(Node p) {
	if (p && --p->count <= 0)
		{ assert(p->x.rmask);
		rmask &= ~sets(p);
		debug(rflag,fprint(2,"deallocating %s from node #%d\n", rnames(sets(p)), p->x.id)); }
}

/* ralloc - assign a register for p */
static void ralloc(Node p) {
	int i;

	assert(p);
	assert(p->x.rmask == 0);
	switch (generic(p->op)) {
	case ARG:
		argoffset = roundup(argoffset, p->syms[1]->u.c.v.i);
		p->x.argoffset = argoffset;
		argoffset += p->syms[0]->u.c.v.i;
		if (argoffset > argbuildsize)
			argbuildsize = roundup(argoffset, 4);
		break;
	case CALL:
		argoffset = 0;
		break;
	default:assert(valid(p->op));
	}
	p->x.busy = rmask;	/* Have switched these over...*/
	if (needsreg(p))
		getreg(p);
	for (i = 0; i < MAXKIDS; i++)
		putreg(p->kids[i]);
}

/* segment - switch to logical segment s */
void segment(int s) {
	return;
	switch (s) {
	case CODE: print(".text\n");   break;
	case  LIT: print(".text 1\n"); break;
	case DATA:
	case  BSS: print(".data\n");   break;
	default: assert(0);
	}
}

/* spill - spill all registers that overlap (r,m) */
static void spill(int r, unsigned m, Node dot) {
	int i;
	Node p = dot;

	while (p = p->x.next)
		for (i = 0; i < MAXKIDS; i++)
			if (p->kids[i] && sets(p->kids[i])&(m<<r)) {
				Symbol temp = genspill(p->kids[i]);
				rmask &= ~sets(p->kids[i]);
				genreloads(dot, p->kids[i], temp);
			}
}

/* spillee - identify the most-distantly-used register */
static int spillee(Node dot, unsigned m) {
	int bestdist = -1, bestreg = 0, dist, r;
	Node q;

	debug(rflag,fprint(2,"spillee: dot is node #%d\n", dot->x.id));
	for (r = 1; r < nregs - (m>>1); r++) {
		dist = 0;
		for (q = dot->x.next; q && !(uses(q)&(m<<r)); q = q->x.next)
			dist++;
		assert(q);	/* (omit) */
		debug(rflag,fprint(2,"r%d used in node #%d at distance %d\n", r, q->x.id, dist));
		if (dist > bestdist) {
			bestdist = dist;
			bestreg = r;
		}
	}
	debug(rflag,fprint(2,"spilling %s\n",rnames(m<<bestreg)));
	assert(bestreg);	/* (omit) */
	return bestreg;
}

/* uses - return mask of registers used by node p */
static unsigned uses(Node p) {
	int i;
	unsigned m = 0;

	for (i = 0; i < MAXKIDS; i++)
		if (p->kids[i])
			m |= sets(p->kids[i]);
	return m;
}

/* valid - is operator op a valid operator ? */
static int valid(op) {
	return opindex(op) > 0 && opindex(op) < sizeof reginfo/sizeof reginfo[0] ?
		reginfo[opindex(op)]&(1<<optype(op)) : 0;
}

#ifdef DEBUG
/* lprint - print the nodelist beginning at p */
static void lprint(Node p, char *s) {
	fprint(2, "node list%s:\n", s);
	if (p) {
		char buf[100];
		sprintf(buf, "%-4s%-8s%-8s%-8s%-7s%-13s%s",
			" #", "op", "kids", "syms", "count", "uses", "sets");
		fprint(2, "%s\n", buf);
	}
	for ( ; p; p = p->x.next)
		nprint(p);
}

/* nprint - print a line describing node p */
static void nprint(Node p) {
	int i;
	char *kids = "", *syms = "", buf[200];

	if (p->kids[0]) {
		static char buf[100];
		buf[0] = 0;
		for (i = 0; i < MAXKIDS && p->kids[i]; i++)
			sprintf(buf + strlen(buf), "%3d", p->kids[i]->x.id);
		kids = &buf[1];
	}
	if (p->syms[0] && p->syms[0]->x.name) {
		static char buf[100];
		buf[0] = 0;
		for (i = 0; i < MAXSYMS && p->syms[i]; i++) {
			if (p->syms[i]->x.name)
				sprintf(buf + strlen(buf), " %s", p->syms[i]->x.name);
			if (p->syms[i]->u.c.loc)
				sprintf(buf + strlen(buf), "=%s", p->syms[i]->u.c.loc->name);
		}
		syms = &buf[1];
	}
	sprintf(buf, "%2d. %-8s%-8s%-8s %2d    %-13s",
		p->x.id, opname(p->op), kids, syms, p->count, rnames(uses(p)));
	sprintf(buf + strlen(buf), "%s", rnames(sets(p)));
	fprint(2, "%s\n", buf);
}

/* rnames - return names of registers given by mask m */
static char *rnames(unsigned m) {
	static char buf[100];
	int r;

	buf[0] = buf[1] = 0;
	for (r = 0; r < nregs; r++)
		if (m&(1<<r))
			sprintf(buf + strlen(buf), " r%d", r);
	return &buf[1];
}
#endif

#ifndef space
void space(int i)
{
int j;
for(j=0;j<i;j++)
    print(" BRK\n");
}
#endif

#ifndef V9
#include <errno.h>
#ifndef errno
extern int errno;
#endif

/* strtol - interpret str as a base b number; if ptr!=0, *ptr gets updated str */
long strtol(str, ptr, b) char *str, **ptr; {
	long n = 0;
	char *s, sign = '+';
	int d, overflow = 0;

	if (ptr)
		*ptr = str;
	if (b < 0 || b == 1 || b > 36)
		return 0;
	while (*str==' '||*str=='\f'||*str=='\n'||*str=='\r'||*str=='\t'||*str=='\v')
		str++;
	if (*str == '-' || *str == '+')
		sign = *str++;
	if (b == 0)
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
			b = 16;
			str += 2;
		} else if (str[0] == '0')
			b = 8;
		else
			b = 10;
	for (s = str; *str; str++) {
		if (*str >= '0' && *str <= '9')
			d = *str - '0';
		else if (*str >= 'a' && *str <= 'z' || *str >= 'A' && *str <= 'Z')
			d = (*str&~040) - 'A' + 10;
		else
			break;
		if (d >= b)
			break;
		if (n < (LONG_MIN + d)/b)
			overflow = 1;
		n = b*n - d;
	}
	if (s == str)
		return 0;
	if (ptr)
		*ptr = str;
	if (overflow || (sign == '+' && n == LONG_MIN)) {
		errno = ERANGE;
		return sign == '+' ? LONG_MAX : LONG_MIN;
	}
	return sign == '+' ? -n : n;
}
#endif
