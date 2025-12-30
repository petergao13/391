/*! @file elf.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief ELF file loader
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#include "console.h"
#ifdef ELF_TRACE
#define TRACE
#endif

#ifdef ELF_DEBUG
#define DEBUG
#endif

#include "elf.h"

#include <stdint.h>

#include "conf.h"
#include "error.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "uio.h"
#include "heap.h"
#include <thread.h>

// Offsets into e_ident

#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8
#define EI_PAD 9

// ELF header e_ident[EI_CLASS] values

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

// ELF header e_ident[EI_DATA] values

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF header e_ident[EI_VERSION] values

#define EV_NONE 0
#define EV_CURRENT 1

// ELF header e_type values

enum elf_et { ET_NONE = 0, ET_REL, ET_EXEC, ET_DYN, ET_CORE };

/*! @struct elf64_ehdr
    @brief ELF header struct
*/
struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

/*! @enum elf_pt
    @brief Program header p_type values
*/
enum elf_pt { PT_NULL = 0, PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_NOTE, PT_SHLIB, PT_PHDR, PT_TLS };

// Program header p_flags bits

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

/*! @struct elf64_phdr
    @brief Program header struct
*/
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// ELF header e_machine values (short list)

#define EM_RISCV 243
/**
 * \brief Validates and loads an ELF file into memory.
 *
 * This function validates an ELF file, then loads its contents into memory,
 * returning the start of the entry point through \p eptr.
 *
 * The loader processes only program header entries of type `PT_LOAD`. The layouts
 * of structures and magic values can be found in the Linux ELF header file
 * `<uapi/linux/elf.h>`
 * The implementation should ensure that all loaded sections of the program are
 * mapped within the memory range `0x80100000` to `0x81000000`.
 * OK WE CHANGE THIS TO 0x0C0000000UL and 0x100000000UL instead for CP2??
 *
 * Let's do some reading! The following documentation will be very helpful!
 * [Helpful doc](https://linux.die.net/man/5/elf)
 * Good luck!
 * [Educational video](https://www.youtube.com/watch?v=dQw4w9WgXcQ)
 *
 * \param[in]  uio  Pointer to an user I/O corresponding to the ELF file.
 * \param[out] eptr   Double pointer used to return the ELF file's entry point.
 *
 * \return 0 on success, or a negative error code on failure.
 */
int elf_load(struct uio* uio, void (**eptr)(void)) {
    // FIXME
    if (uio == NULL || eptr == NULL) {
        return -EINVAL;
    }

    struct elf64_ehdr * elf_header = kcalloc(1, sizeof(struct elf64_ehdr));
    if (elf_header == NULL) {
        return -ENOMEM;
    }
    struct elf64_phdr * program_header = kcalloc(1, sizeof(struct elf64_phdr));
    if (program_header == NULL) {
        kfree(elf_header);
        return -ENOMEM;
    }
    unsigned long long pos;
    int result;
    
    // read elf header
    pos = 0;
    result = uio_cntl(uio, FCNTL_SETPOS, &pos);
    if(result < 0){
        kfree(elf_header);
        kfree(program_header);
        return result;
    }
    result = uio_read(uio, (void*)elf_header, sizeof(struct elf64_ehdr));
    if(result < 0){
        kfree(elf_header);
        kfree(program_header);
        return result;
    }
    
    //validate elf header values
    if ((elf_header->e_ident[0] != 0x7F) || (elf_header->e_ident[1] != 'E') || (elf_header->e_ident[2] != 'L') || (elf_header->e_ident[3] != 'F')) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL;
    }

    if (elf_header->e_ident[EI_CLASS] != ELFCLASS64 || elf_header->e_ident[EI_DATA] != ELFDATA2LSB || elf_header->e_ident[EI_VERSION] != EV_CURRENT) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL; 
    }

    //check if its a execulateble
    if (elf_header->e_type != ET_EXEC) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL; 
    }
    
    if (elf_header->e_machine != EM_RISCV) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL; 
    }

    if (elf_header->e_version != EV_CURRENT) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL; 
    }

    if (elf_header->e_entry < UMEM_START_VMA || elf_header->e_entry >= UMEM_END_VMA) {
        kfree(elf_header);
        kfree(program_header);
        return -EINVAL; 
    }
    
    for(int i = 0; i < elf_header->e_phnum; i++){
        // read program header
        pos = elf_header->e_phoff + (i * elf_header->e_phentsize);
        result = uio_cntl(uio, FCNTL_SETPOS, &pos);
        if(result < 0){
            kfree(elf_header);
            kfree(program_header);
            return result;
        }
        result = uio_read(uio, (void*)program_header, sizeof(struct elf64_phdr));
        if(result < 0){
            kfree(elf_header);
            kfree(program_header);
            return result;
        }
        
        //validate each program header entries
        //memcpy import info into memory
        //only want to copy if we are the loading type  
        if(program_header->p_type == PT_LOAD){
            //check if we are within the memory range `0x80100000` to `0x81000000`
            //UPDATE FOR CP2 TO 0x0C0000000UL and 0x100000000UL
            if (program_header->p_vaddr < UMEM_START_VMA || program_header->p_vaddr + program_header->p_memsz >= UMEM_END_VMA) {
                kfree(elf_header);
                kfree(program_header);
                return -EIO;
            }

            //You will need to
            //allocate and map the appropriate amount of pages, as well as set the appropriate flags in the page table
            //when mapping the program segments.
            
            
            int pte_flags = PTE_U; //have to check one at a time
            if((program_header->p_flags & PF_R)) {
                pte_flags |= PTE_R;
            }
            if((program_header->p_flags & PF_X)) {
                pte_flags |= PTE_X;
            }
            if((program_header->p_flags & PF_W)){
                pte_flags |= PTE_W;
            }

            alloc_and_map_range(program_header->p_vaddr, program_header->p_memsz, PTE_W | PTE_R | PTE_U);
            

            pos = program_header->p_offset;
            result = uio_cntl(uio, FCNTL_SETPOS, &pos);
            if(result < 0){
                kfree(elf_header);
                kfree(program_header);
                return result;
            }
            
            //basically already memcpys
            result = uio_read(uio, (void*)program_header->p_vaddr, program_header->p_filesz);
            
            if(result < 0){
                kfree(elf_header);
                kfree(program_header);
                return result;
            }


            /*If the segment's memory size p_memsz is larger than the file size p_filesz, 
            the "extra" bytes are defined to hold the value 0 and to follow the segment's initialized area. 
            The file size may not be larger than the memory size.*/
            if (program_header->p_memsz > program_header->p_filesz) {
                memset((void*)(program_header->p_vaddr + program_header->p_filesz), 0, program_header->p_memsz - program_header->p_filesz);
            }


            //trying this??
            set_range_flags((void *)program_header->p_vaddr, program_header->p_memsz, pte_flags);
        }
    }

    *eptr = (void*)elf_header->e_entry;

    kfree(elf_header);
    kfree(program_header);

    return 0;
}