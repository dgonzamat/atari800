; LOADFIX.COM minimo para DOS 486 (nasm -f bin loadfix.asm -o LOADFIX.COM)
; Uso: LOADFIX programa.exe [argumentos]
; Reduce su propio bloque de memoria a 64 KB y ejecuta el programa por encima,
; para que los EXE comprimidos con EXEPACK no se carguen en los primeros 64 KB.
        org 100h
start:  mov ah, 4Ah                 ; redimensionar bloque propio a 64 KB
        mov bx, 1000h
        push cs
        pop es
        int 21h
        mov si, 81h                 ; cola de comandos
        mov di, prog
skipsp: lodsb
        cmp al, ' '
        je skipsp
        cmp al, 0Dh
        je usage
copyn:  stosb                       ; copiar nombre del programa
        lodsb
        cmp al, ' '
        je endn
        cmp al, 0Dh
        je endn
        jmp copyn
endn:   mov byte [di], 0
        dec si                      ; resto de la linea = argumentos
        mov di, tail+1
        xor cx, cx
copyt:  lodsb
        stosb
        cmp al, 0Dh
        je donet
        inc cx
        jmp copyt
donet:  mov [tail], cl
        mov [pb_tail+2], cs
        mov [pb_fcb1+2], cs
        mov [pb_fcb2+2], cs
        mov ax, 4B00h               ; EXEC
        mov dx, prog
        mov bx, pblock
        push cs
        pop es
        int 21h
        jc fail
        mov ah, 4Dh                 ; codigo de retorno del hijo
        int 21h
        mov ah, 4Ch
        int 21h
fail:   mov dx, msgerr
        mov ah, 9
        int 21h
        mov ax, 4C01h
        int 21h
usage:  mov dx, msguse
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h
msguse: db 'Uso: LOADFIX programa [argumentos]', 0Dh, 0Ah, '$'
msgerr: db 'LOADFIX: no se pudo ejecutar el programa', 0Dh, 0Ah, '$'
pblock: dw 0                        ; entorno heredado
pb_tail: dw tail, 0
pb_fcb1: dw 5Ch, 0
pb_fcb2: dw 6Ch, 0
tail:   times 130 db 0
prog:   times 130 db 0
