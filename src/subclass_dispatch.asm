; Samandarin can release and unmap the SPL synchronously inside DefSubclassProc.
; The forwarding path must leave no return address in the SPL, even in Debug.
EXTERN __imp_DefSubclassProc:QWORD
.code
DispatchSubclass PROC FRAME
    ; 32 bytes home space, two stack arguments, one forwarding flag.
    sub rsp, 56
    .allocstack 56
    .endprolog
    ; Save the original four register arguments in the caller's home space.
    mov [rsp+64], rcx
    mov [rsp+72], rdx
    mov [rsp+80], r8
    mov [rsp+88], r9
    mov byte ptr [rsp+48], 0
    lea rax, [rsp+48]
    mov [rsp+32], rax
    mov rax, [rsp+104]        ; dwRefData
    mov [rsp+40], rax
    call qword ptr [rsp+96]   ; uIdSubclass identifies the typed handler
    cmp byte ptr [rsp+48], 0
    jne forwardMessage
    add rsp, 56
    ret
forwardMessage:
    mov rcx, [rsp+64]
    mov rdx, [rsp+72]
    mov r8, [rsp+80]
    mov r9, [rsp+88]
    add rsp, 56
    jmp qword ptr [__imp_DefSubclassProc]
DispatchSubclass ENDP
END
