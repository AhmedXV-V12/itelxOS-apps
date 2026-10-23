/* SQLite on itelx: character classes (libitx and the kit) */
#ifndef SHIM_CTYPE_H
#define SHIM_CTYPE_H
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int ispunct(int c);
int toupper(int c);
int tolower(int c);
#endif
