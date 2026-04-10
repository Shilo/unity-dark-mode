; version_proxy.asm — x64 MASM trampolines for version.dll proxy
;
; Each trampoline is a single indirect JMP through the g_originalFuncs table.
; Because x64 uses a register-based calling convention (rcx, rdx, r8, r9 + stack),
; an indirect JMP preserves all arguments and returns directly to the original caller.

EXTERN g_originalFuncs:QWORD

.CODE

Proxy_GetFileVersionInfoA       PROC
    jmp QWORD PTR [g_originalFuncs + 0*8]
Proxy_GetFileVersionInfoA       ENDP

Proxy_GetFileVersionInfoByHandle PROC
    jmp QWORD PTR [g_originalFuncs + 1*8]
Proxy_GetFileVersionInfoByHandle ENDP

Proxy_GetFileVersionInfoExA     PROC
    jmp QWORD PTR [g_originalFuncs + 2*8]
Proxy_GetFileVersionInfoExA     ENDP

Proxy_GetFileVersionInfoExW     PROC
    jmp QWORD PTR [g_originalFuncs + 3*8]
Proxy_GetFileVersionInfoExW     ENDP

Proxy_GetFileVersionInfoSizeA   PROC
    jmp QWORD PTR [g_originalFuncs + 4*8]
Proxy_GetFileVersionInfoSizeA   ENDP

Proxy_GetFileVersionInfoSizeExA PROC
    jmp QWORD PTR [g_originalFuncs + 5*8]
Proxy_GetFileVersionInfoSizeExA ENDP

Proxy_GetFileVersionInfoSizeExW PROC
    jmp QWORD PTR [g_originalFuncs + 6*8]
Proxy_GetFileVersionInfoSizeExW ENDP

Proxy_GetFileVersionInfoSizeW   PROC
    jmp QWORD PTR [g_originalFuncs + 7*8]
Proxy_GetFileVersionInfoSizeW   ENDP

Proxy_GetFileVersionInfoW       PROC
    jmp QWORD PTR [g_originalFuncs + 8*8]
Proxy_GetFileVersionInfoW       ENDP

Proxy_VerFindFileA              PROC
    jmp QWORD PTR [g_originalFuncs + 9*8]
Proxy_VerFindFileA              ENDP

Proxy_VerFindFileW              PROC
    jmp QWORD PTR [g_originalFuncs + 10*8]
Proxy_VerFindFileW              ENDP

Proxy_VerInstallFileA           PROC
    jmp QWORD PTR [g_originalFuncs + 11*8]
Proxy_VerInstallFileA           ENDP

Proxy_VerInstallFileW           PROC
    jmp QWORD PTR [g_originalFuncs + 12*8]
Proxy_VerInstallFileW           ENDP

Proxy_VerLanguageNameA          PROC
    jmp QWORD PTR [g_originalFuncs + 13*8]
Proxy_VerLanguageNameA          ENDP

Proxy_VerLanguageNameW          PROC
    jmp QWORD PTR [g_originalFuncs + 14*8]
Proxy_VerLanguageNameW          ENDP

Proxy_VerQueryValueA            PROC
    jmp QWORD PTR [g_originalFuncs + 15*8]
Proxy_VerQueryValueA            ENDP

Proxy_VerQueryValueW            PROC
    jmp QWORD PTR [g_originalFuncs + 16*8]
Proxy_VerQueryValueW            ENDP

END
