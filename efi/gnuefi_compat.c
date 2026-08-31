/*
 * Copyright 2011 Intel Corporation - All Rights Reserved
 *
 * gnu-efi >= 4.0 renamed some of its entry points and no longer exports
 * the plain symbols that the klibc-based core/com32 code and the EFI
 * firmware link still reference:
 *
 *   - setjmp()/longjmp() were renamed to base_setjmp()/base_longjmp()
 *     (exposed as macros in <efisetjmp.h>).  The gnu-efi assembly writes
 *     exactly the same jmp_buf layout as klibc's struct __jmp_buf, so a
 *     direct forward is ABI-correct.
 *
 *   - CopyMem() was renamed to CopyMem_1(); the intended
 *     #define CopyMem(a,b,c) CopyMem_1(a,b,c) lives in
 *     <legacy/efilib.h>, which the gnu-efi library sources themselves do
 *     not include, so objects such as lib/event.o emit an undefined
 *     CopyMem reference.
 */

#include <setjmp.h>

#define EXPORT __attribute__((visibility("default")))

unsigned long base_setjmp(void *env) __attribute__((returns_twice));
void base_longjmp(void *env, unsigned long value) __attribute__((noreturn));

EXPORT int setjmp(jmp_buf env)
{
	return (int)base_setjmp(env);
}

EXPORT __attribute__((noreturn))
void longjmp(jmp_buf env, int value)
{
	base_longjmp(env, (unsigned long)value);
	__builtin_unreachable();
}

#if defined(__x86_64__) && !defined(_WIN32)
__attribute__((ms_abi))
#endif
void CopyMem_1(void *Dest, void *Src, unsigned long len);

EXPORT __attribute__((weak))
void CopyMem(void *Dest, void *Src, unsigned long len)
{
	CopyMem_1(Dest, Src, len);
}
