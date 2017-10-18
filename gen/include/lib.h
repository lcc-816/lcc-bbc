extern void exit(int retval);

extern int open(char *name,int rwmode);
extern int creat(char *name,int pmode);
extern void close(int fd);
extern void unlink(char *name);

extern void system(char *string);

extern int read(int fd,char *buf, int count);
extern int write(int fd,char *buf, int count);

extern int getc(int fd);
extern int getchar(void);
extern void putc(char c, int fd);

extern void vdu(char c);
extern int osbyte(char type,char param1,char param2);
extern int osword(char type,char *address);
extern int stat(char *name,char *fcb);
extern int osfile(char *name,char * fcb,char type);

extern int isalpha(char c);
extern int isupper(char c);
extern int islower(char c);
extern int isdigit(char c);
extern int isspace(char c);
extern int ispunct(char c);
extern int isprint(char c);
extern int iscntrl(char c);
extern int isascii(char c);
extern char toupper(char c);
extern char tolower(char c);
extern char toascii(char c);

extern int _cmdlin(void);

extern char* sbrk();
extern char* brk();

extern char *strcpy(char *s1,char * s2);
extern int strcmp(char *s1,char * s2);
