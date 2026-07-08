# Declare constants for Multiboot headers
.set MULTIBOOT1_MAGIC,     0x1BADB002
.set MULTIBOOT1_FLAGS,     0x00000003  # ALIGN + MEMINFO
.set MULTIBOOT1_CHECKSUM,  -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

.set MULTIBOOT2_MAGIC,     0xe85250d6
.set MULTIBOOT2_ARCH,      0           # 32-bit (protected mode) of i386
.set HEADER_LENGTH,        (multiboot2_end - multiboot2_start)
.set MULTIBOOT2_CHECKSUM,  -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + HEADER_LENGTH)

.section .multiboot_header
.align 8

# --- Multiboot 1 Header ---
multiboot1_start:
    .long MULTIBOOT1_MAGIC
    .long MULTIBOOT1_FLAGS
    .long MULTIBOOT1_CHECKSUM
multiboot1_end:

.align 8
# --- Multiboot 2 Header ---
multiboot2_start:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH
    .long HEADER_LENGTH
    .long MULTIBOOT2_CHECKSUM

    # End tag (required by Multiboot2 spec)
    .short 0   # Type
    .short 0   # Flags
    .long 8    # Size
multiboot2_end:

.section .text
.code32
.global boot_entry
boot_entry:
    # Disable interrupts
    cli

    # Verify multiboot magic (allow Multiboot1 or Multiboot2 magic)
    cmpl $0x36d76289, %eax      # Multiboot2 magic
    je .magic_ok
    cmpl $0x2badb002, %eax      # Multiboot1 magic
    je .magic_ok
    jmp no_multiboot

.magic_ok:

    # Set up a temporary stack for 32-bit mode execution
    movl $boot_stack_top, %esp

    # Reset EFLAGS
    pushl $0
    popf

    # --- Set up Page Tables for 64-bit Transition ---
    # PML4[0] -> PDPT
    movl $pdpt, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pml4

    # PDPT[0] -> PD
    movl $pd, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pdpt

    # PD entries -> Map 512 entries of 2MB (Total 1GB identity map)
    movl $0, %ecx
.map_pd_loop:
    movl %ecx, %eax
    shll $21, %eax      # EAX = ECX * 2MB
    orl $0x83, %eax     # Present + Writable + Page Size (2MB huge page)
    
    # Store into 64-bit entry (lower 32-bit in EAX, upper 32-bit as 0)
    movl %eax, pd(,%ecx,8)
    movl $0, pd+4(,%ecx,8)

    incl %ecx
    cmpl $512, %ecx
    jne .map_pd_loop

    # --- Enable Paging and Long Mode ---
    # Load PML4 address to CR3
    movl $pml4, %eax
    movl %eax, %cr3

    # Enable PAE (Physical Address Extension) by setting bit 5 of CR4
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    # Enable Long Mode by setting LME (Long Mode Enable) bit (bit 8) in EFER MSR (0xC0000080)
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    # Enable Paging by setting PG (bit 31) and PE (bit 0) in CR0
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    # Load 64-bit Global Descriptor Table
    lgdt gdt64_ptr

    # Perform far jump to 64-bit code segment using lret
    pushl $0x08                 # Code segment selector (index 1 in GDT, RPL 0)
    pushl $long_mode_start      # Address of 64-bit entry
    lret

no_multiboot:
    # Write a simple error character 'M' to VGA screen (0xB8000) and halt
    movw $0x4F4D, 0xB8000
    cli
.halt:
    hlt
    jmp .halt


.code64
.global long_mode_start
long_mode_start:
    # Reload 64-bit data segments
    movw $0x10, %ax             # Data segment selector (index 2 in GDT)
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    # Set up the 64-bit stack
    movq $boot_stack_top, %rsp

    # Clear RBP to start a clean stack trace
    xorq %rbp, %rbp

    # Call C++ kernel entry point
    call kmain

    # In case kmain returns, disable interrupts and halt
.halt_64:
    cli
    hlt
    jmp .halt_64


# --- Boot Data Structures ---
.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000    # Null descriptor
    .quad 0x00209a0000000000    # 64-bit Code descriptor (exec/read)
    .quad 0x0000920000000000    # 64-bit Data descriptor (read/write)
gdt64_ptr:
    .short . - gdt64 - 1
    .quad gdt64


.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096

.align 16
boot_stack_bottom:
    .skip 16384                 # 16KB stack
boot_stack_top:
