# Declare constants for Multiboot headers
.set MULTIBOOT1_MAGIC,     0x1BADB002
.set MULTIBOOT1_FLAGS,     0x00000007  # ALIGN + MEMINFO + GRAPHICS
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
    # Address fields (must be present if flag 2 or 16 is set, set to 0 as ELF loader handles execution)
    .long 0
    .long 0
    .long 0
    .long 0
    .long 0
    # Graphics parameters
    .long 0             # 0 = Linear Graphics Mode (framebuffer)
    .long 1920          # Width
    .long 1080          # Height
    .long 32            # BPP
multiboot1_end:

.align 8
# --- Multiboot 2 Header ---
multiboot2_start:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH
    .long HEADER_LENGTH
    .long MULTIBOOT2_CHECKSUM

    # Framebuffer Tag (Type 5)
    .align 8
    .short 5            # Type
    .short 0            # Flags
    .long 20            # Size
    .long 1920          # Width
    .long 1080          # Height
    .long 32            # BPP

    # End tag (required by Multiboot2 spec)
    .align 8
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
    # Save Multiboot parameters (eax = magic, ebx = MBI physical address)
    movl %eax, %edi
    movl %ebx, %esi

    # Set up a temporary stack for 32-bit mode execution
    movl $boot_stack_top, %esp

    # Reset EFLAGS
    pushl $0
    popf

    # --- Set up Page Tables for 64-bit Transition ---
    # PML4[0] (identity map) -> PDPT
    movl $pdpt, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pml4

    # PML4[256] (Direct Physical Map base 0xFFFF800000000000) -> PDPT
    movl %eax, pml4 + 256 * 8

    # PML4[511] (Kernel virtual base 0xFFFFFFFF80000000) -> PDPT
    movl %eax, pml4 + 511 * 8

    # PDPT[0..3] -> pd0..pd3 (maps 0GB - 4GB region)
    movl $pd0, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pdpt
    movl %eax, pdpt + 510 * 8 # Map 0xFFFFFFFF80000000 -> pd0 (first 2GB)

    movl $pd1, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pdpt+8
    movl %eax, pdpt + 511 * 8 # Map 0xFFFFFFFFC0000000 -> pd1 (second 2GB)

    movl $pd2, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pdpt+16

    movl $pd3, %eax
    orl $3, %eax       # Present + Writable
    movl %eax, pdpt+24

    # Contiguous pd0..pd3 entries -> Map 2048 entries of 2MB (Total 4GB identity map)
    movl $0, %ecx
.map_pd_loop:
    movl %ecx, %eax
    shll $21, %eax      # EAX = ECX * 2MB
    orl $0x83, %eax     # Present + Writable + Page Size (2MB huge page)
    
    # Store into 64-bit entries inside pd0..pd3 contiguous space
    movl %eax, pd0(,%ecx,8)
    movl $0, pd0+4(,%ecx,8)

    incl %ecx
    cmpl $2048, %ecx
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

    # Call C++ kernel entry point absolutely in the higher half
    movabsq $kmain, %rax
    call *%rax

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


.section .boot
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd0:
    .skip 4096
pd1:
    .skip 4096
pd2:
    .skip 4096
pd3:
    .skip 4096

.section .bss
.align 16
boot_stack_bottom:
    .skip 16384                 # 16KB stack
.global boot_stack_top
boot_stack_top:
