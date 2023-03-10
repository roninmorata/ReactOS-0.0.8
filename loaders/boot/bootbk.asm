;
; File:
;			     boot.asm
; Description:
;			    DOS-C boot
;
;			Copyright (c) 1997;			
;			    Svante Frey
;			All Rights Reserved
;
; This file is part of DOS-C.
;
; DOS-C is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version
; 2, or (at your option) any later version.
;
; DOS-C is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
; the GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public
; License along with DOS-C; see the file COPYING.  If not,
; write to the Free Software Foundation, 675 Mass Ave,
; Cambridge, MA 02139, USA.
;
; $Logfile:   C:/dos-c/src/boot/boot.asv  $
;
; $Header: /cygdrive/c/RCVS/CVS/ReactOS/reactos/loaders/boot/Attic/bootbk.asm,v 1.1.1.2 1998/08/25 04:27:38 rex Exp $
;
; $Log: bootbk.asm,v $
; Revision 1.1.1.2  1998/08/25 04:27:38  rex
; A much Needed Update
;
;	
;	   Rev 1.5   10 Jan 1997  4:58:06   patv
;	Corrected copyright
;	
;	   Rev 1.4   10 Jan 1997  4:52:50   patv
;	Re-written to support C drive and eliminate restrictions on IPL.SYS
;	
;	   Rev 1.3   29 Aug 1996 13:06:50   patv
;	Bug fixes for v0.91b
;	
;	   Rev 1.2   01 Sep 1995 17:56:44   patv
;	First GPL release.
;	
;	   Rev 1.1   30 Jul 1995 20:37:38   patv
;	Initialized stack before use.
;	
;	   Rev 1.0   02 Jul 1995 10:57:52   patv
;	Initial revision.
;

.text

BASE            equ     0


		org     BASE
Entry:          jmp     real_start

;	bp is initialized to 7c00h
oem             equ     [bp+3]
bytesPerSector  equ     [bp+0bh]
sectPerCluster  equ     [bp+0dh]
resSectors      equ     [bp+0eh]
nFats           equ     [bp+10h]
nRootDir        equ     [bp+11h]
nSectors        equ     [bp+13h]
MID             equ     [bp+15h]
sectPerFat      equ     [bp+16h]
sectPerTrack    equ     [bp+18h]
nHeads          equ     [bp+1ah]
nHidden         equ     [bp+1ch]
nSectorHuge     equ     [bp+20h]
drive           equ     [bp+24h]
extBoot         equ     [bp+26h]
volid           equ     [bp+27h]
vollabel        equ     [bp+2bh]
filesys         equ     [bp+36h]

LOADSEG         equ     2000h

FATBUF          equ     4000h           ; offset of temporary buffer for FAT 
					; chain
RETRYCOUNT      equ     5               ; number of retries on disk errors

;	Some extra variables that are created on the stack frame

fat_start       equ     [bp-4]          ; first FAT sector
root_dir_start  equ     [bp-8]          ; first root directory sector
data_start      equ     [bp-12]         ; first data sector


;
; Include macros for filesystem access
;
include boot.inc

;
;
;
		org     BASE+3eh

tempbuf         equ     [bp+3eh]
load_seg        dw      LOADSEG

real_start:     cli
		cld
		mov     ax, cs
		mov     ss, ax          ; initialize stack      
		mov     bp, 7c00h
		lea     sp, [bp-20h]
		sti

		mov     es, ax
		mov     ds, ax
		mov     drive, dl       ; BIOS passes drive number in DL

		GETDRIVEPARMS

		FINDFILE                ; locate file in root directory
		jc      boot_error      ; fail if not found

		GETFATCHAIN             ; read FAT chain
		LOADFILE                ; load file (jumps to boot_sucess if successful)

boot_error:     mov     cx, ERRMSGLEN
		mov     si, offset errmsg+7c00h

next_char:      lodsb                   ; print error message
		mov     ah, 0eh
		xor     bh, bh
		int     10h
		loop    next_char

		xor     ah, ah
		int     16h             ; wait for keystroke
		int     19h             ; invoke bootstrap loader

boot_success:   mov     dl, drive

		db      0eah            ; far jump to LOADSEG:0000
		dw      0
		dw      LOADSEG


;	readDisk:       Reads a number of sectors into memory.
;
;	Call with:      DX:AX = 32-bit DOS sector number
;	                DI = number of sectors to read
;	                ES:BX = destination buffer
;	                ES must be 64k aligned (1000h, 2000h etc).
;
;	Returns:        CF set on error
;	                ES:BX points one byte after the last byte read. 

readDisk        proc
		push    si
read_next:      push    dx
		push    ax

		;
		; translate sector number to BIOS parameters
		;

		;
		; abs = sector                          offset in track
		;     + head * sectPerTrack             offset in cylinder
		;     + track * sectPerTrack * nHeads   offset in platter
		; 
		; t1     = abs  /  sectPerTrack         (ax has t1)
		; sector = abs mod sectPerTrack         (cx has sector)
		;
		div     word ptr sectPerTrack
		mov     cx, dx

		;
		; t1   = head + track * nHeads
		;
		; track = t1  /  nHeads                 (ax has track)
		; head  = t1 mod nHeads                 (dl has head)
		;
		xor     dx, dx
		div     word ptr nHeads

		; the following manipulations are necessary in order to 
		; properly place parameters into registers.
		; ch = cylinder number low 8 bits
		; cl = 7-6: cylinder high two bits
		;      5-0: sector
		mov     dh, dl                  ; save head into dh for bios
		ror     ah, 1                   ; move track high bits into
		ror     ah, 1                   ; bits 7-6 (assumes top = 0)
		xchg    al, ah                  ; swap for later
		mov     dl, byte ptr sectPerTrack
		sub     dl, cl
		inc     cl                      ; sector offset from 1
		or      cx, ax                  ; merge cylinder into sector
		mov     al, dl                  ; al has # of sectors left

		; Calculate how many sectors can be transfered in this read
		; due to dma boundary conditions.
		push    dx

		mov     si, di                  ; temp register save
		; this computes remaining bytes because of modulo 65536
		; nature of dma boundary condition
		mov     ax, bx                  ; get offset pointer
		neg     ax                      ; and convert to bytes
		jz      ax_min_1                ; started at seg:0, skip ahead

		xor     dx, dx                  ; convert to sectors
		div     word ptr bytesPerSector
		
		cmp     ax, di                  ; check remainder vs. asked
		jb      ax_min_1                ; less, skip ahead
		mov     si, ax                  ; transfer only what we can

ax_min_1:       pop     dx

		; Check that request sectors do not exceed track boundary
		mov     si, sectPerTrack
		inc     si
		mov     ax, cx                  ; get the sector/cyl byte
		and     ax, 03fh                ; and mask out sector
		sub     si, ax                  ; si has how many we can read
		mov     ax, di
		cmp     si, di                  ; see if asked <= available
		jge     ax_min_2
		mov     ax, si                  ; get what can be xfered

ax_min_2:       mov     si, RETRYCOUNT
		mov     ah, 2
		mov     dl, drive

retry:          push    ax
		int     13h
		pop     ax
		jnc     read_ok
		push    ax
		xor     ax, ax          ; reset the drive
		int     13h
		pop     ax
		dec     si
		jnz     retry
		stc
		pop     ax
		pop     dx
		pop     si
		ret

read_next_jmp:  jmp     short read_next
read_ok:        xor     ah, ah                          
		mov     si, ax                  ; AX = SI = number of sectors read      
		mul     word ptr bytesPerSector ; AX = number of bytes read
		add     bx, ax                  ; add number of bytes read to BX
		jnc     no_incr_es              ; if overflow...

		mov     ax, es       
		add     ah, 10h                 ; ...add 1000h to ES
		mov     es, ax
		
no_incr_es:     pop     ax
		pop     dx                      ; DX:AX = last sector number

		add     ax, si
		adc     dx, 0                   ; DX:AX = next sector to read
		sub     di, si                  ; if there is anything left to read,
		jg      read_next_jmp           ; continue

		clc
		pop     si
		ret
readDisk        endp

errmsg          db      "Boot error"
ERRMSGLEN       equ     $ - errmsg


;filename        db      "OSLDR   BIN"
filename        db      "KERNEL  BIN"

		org     BASE+01feh
sign            dw      0aa55h

TEXT            ENDS
		end




