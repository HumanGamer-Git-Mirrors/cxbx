// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->Cxbx->EmuExe.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#include "Cxbx.h"

#include "EmuExe.h"
#include "Prolog.h"
#include "Kernel.h"

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#include <windows.h>

#include <memory.h>

// ******************************************************************
// * constructor
// ******************************************************************
EmuExe::EmuExe(Xbe *x_Xbe, uint32 x_debug_console) : Exe()
{
    ConstructorInit();

    // ******************************************************************
    // * generate pe header
    // ******************************************************************
    {
        m_Header.m_magic                    = *(uint32 *)"PE\0\0";                      // magic number : "PE\0\0"
        m_Header.m_machine                  = IMAGE_FILE_MACHINE_I386;                  // machine type : i386
        m_Header.m_sections                 = (uint16)(x_Xbe->m_Header.dwSections + 2); // xbe sections + .cxbximp + .cxbxplg
        m_Header.m_timedate                 = x_Xbe->m_Header.dwTimeDate;               // time/date stamp
        m_Header.m_symbol_table_addr        = 0;                                        // unused
        m_Header.m_symbols                  = 0;                                        // unused
        m_Header.m_sizeof_optional_header   = sizeof(OptionalHeader);                   // size of optional header
        m_Header.m_characteristics          = 0x010F;                                   // should be fine..
    }

    // ******************************************************************
    // * generate optional header
    // ******************************************************************
    {
        m_OptionalHeader.m_magic = 0x010B;         // magic number : 0x010B

        // ******************************************************************
        // * abitrary linker version : 6.0
        // ******************************************************************
        m_OptionalHeader.m_linker_version_major = 0x06;
        m_OptionalHeader.m_linker_version_minor = 0x00;

        // ******************************************************************
        // * size of headers
        // ******************************************************************
        m_OptionalHeader.m_sizeof_headers  = sizeof(bzDOSStub) + sizeof(m_Header);
        m_OptionalHeader.m_sizeof_headers += sizeof(m_OptionalHeader) + sizeof(*m_SectionHeader)*m_Header.m_sections;
        m_OptionalHeader.m_sizeof_headers  = RoundUp(m_OptionalHeader.m_sizeof_headers, PE_FILE_ALIGN);

        m_OptionalHeader.m_image_base = x_Xbe->m_Header.dwBaseAddr;

        m_OptionalHeader.m_section_alignment = PE_SEGM_ALIGN;
        m_OptionalHeader.m_file_alignment    = PE_FILE_ALIGN;

        // ******************************************************************
        // * OS version : 4.0
        // ******************************************************************
        m_OptionalHeader.m_os_version_major  = 0x0004;
        m_OptionalHeader.m_os_version_minor  = 0x0000;

        // ******************************************************************
        // * image version : 0.0
        // ******************************************************************
        m_OptionalHeader.m_image_version_major = 0x0000;
        m_OptionalHeader.m_image_version_minor = 0x0000;

        // ******************************************************************
        // * subsystem version : 4.0
        // ******************************************************************
        m_OptionalHeader.m_subsystem_version_major = 0x0004;
        m_OptionalHeader.m_subsystem_version_minor = 0x0000;

        m_OptionalHeader.m_win32_version   = 0x00000000;
        m_OptionalHeader.m_checksum        = 0x00000000;
        m_OptionalHeader.m_subsystem       = IMAGE_SUBSYSTEM_WINDOWS_GUI;

        // ******************************************************************
        // * no special dll characteristics are necessary
        // ******************************************************************
        m_OptionalHeader.m_dll_characteristics = 0x0000;

        // ******************************************************************
        // * TODO: for each of these, check for bad values and correct them
        // ******************************************************************
        m_OptionalHeader.m_sizeof_stack_reserve = 0x00100000;
        m_OptionalHeader.m_sizeof_stack_commit  = 0x00001000;
        m_OptionalHeader.m_sizeof_heap_reserve  = x_Xbe->m_Header.dwPeHeapReserve;
        m_OptionalHeader.m_sizeof_heap_commit   = x_Xbe->m_Header.dwPeHeapCommit;

        // ******************************************************************
        // * this member is obsolete, so we'll just set it to zero
        // ******************************************************************
        m_OptionalHeader.m_loader_flags = 0x00000000;

        // ******************************************************************
        // * we'll set this to the typical 0x10 (16)
        // ******************************************************************
        m_OptionalHeader.m_data_directories = 0x10;

        // ******************************************************************
        // * clear all data directories (we'll setup some later)
        // ******************************************************************
        for(uint32 d=0;d<m_OptionalHeader.m_data_directories;d++)
        {
            m_OptionalHeader.m_image_data_directory[d].m_virtual_addr = 0;
            m_OptionalHeader.m_image_data_directory[d].m_size = 0;
        }
    }

    // ******************************************************************
    // * generate section headers
    // ******************************************************************
    {
        m_SectionHeader = new SectionHeader[m_Header.m_sections];

        // ******************************************************************
        // * start appending section headers at this point
        // ******************************************************************
        uint32 dwSectionCursor = RoundUp(m_OptionalHeader.m_sizeof_headers, 0x1000);

        // ******************************************************************
        // * generate xbe section headers
        // ******************************************************************
        {
            for(uint32 v=0;v<x_Xbe->m_Header.dwSections;v++)
            {
                // ******************************************************************
                // * generate xbe section name
                // ******************************************************************
                {
                    memset(m_SectionHeader[v].m_name, 0, 8);

                    for(int c=0;c<8;c++)
                    {
                        m_SectionHeader[v].m_name[c] = x_Xbe->m_szSectionName[v][c];

                        if(m_SectionHeader[v].m_name[c] == '\0')
                            break;
                    }
                }

                // ******************************************************************
                // * generate xbe section virtual size / addr
                // ******************************************************************
                {
                    uint32 VirtSize = x_Xbe->m_SectionHeader[v].dwVirtualSize;
                    uint32 VirtAddr = x_Xbe->m_SectionHeader[v].dwVirtualAddr - x_Xbe->m_Header.dwBaseAddr;

                    m_SectionHeader[v].m_virtual_size = VirtSize;
                    m_SectionHeader[v].m_virtual_addr = VirtAddr;
                }

                // ******************************************************************
                // * generate xbe section raw size / addr
                // ******************************************************************
                {
                    // TODO: get this working such that m_sizeof_raw can be the actual raw size, not virtual size
                    uint32 RawSize = RoundUp(x_Xbe->m_SectionHeader[v].dwVirtualSize, PE_FILE_ALIGN);
                    uint32 RawAddr = dwSectionCursor;

                    m_SectionHeader[v].m_sizeof_raw = RawSize;
                    m_SectionHeader[v].m_raw_addr   = RawAddr;

                    dwSectionCursor += RawSize;
                }

                // ******************************************************************
                // * relocation / line numbers will not exist
                // ******************************************************************
                {
                    m_SectionHeader[v].m_relocations_addr = 0;
                    m_SectionHeader[v].m_linenumbers_addr = 0;

                    m_SectionHeader[v].m_relocations = 0;
                    m_SectionHeader[v].m_linenumbers = 0;
                }

                // ******************************************************************
                // * generate flags for this xbe section
                // ******************************************************************
                {
                    uint32 flags = IMAGE_SCN_MEM_READ;

                    if(x_Xbe->m_SectionHeader[v].dwFlags.bExecutable)
                    {
                        flags |= IMAGE_SCN_MEM_EXECUTE;
                        flags |= IMAGE_SCN_CNT_CODE;
                    }
                    else
                    {
                        flags |= IMAGE_SCN_CNT_INITIALIZED_DATA;
                    }

                    if(x_Xbe->m_SectionHeader[v].dwFlags.bWritable)
                        flags |= IMAGE_SCN_MEM_WRITE;

                    m_SectionHeader[v].m_characteristics = flags;
                }
            }
        }

        // ******************************************************************
        // * generate .cxbximp section header
        // ******************************************************************
        {
            uint32 i = m_Header.m_sections - 2;

            memcpy(m_SectionHeader[i].m_name, ".cxbximp", 8);

            // ******************************************************************
            // * generate .cxbximp section virtual size / addr
            // ******************************************************************
            {
                uint32 virt_size = RoundUp(0x55, PE_SEGM_ALIGN);
                uint32 virt_addr = RoundUp(m_SectionHeader[i-1].m_virtual_addr + m_SectionHeader[i-1].m_virtual_size, PE_SEGM_ALIGN);

                m_SectionHeader[i].m_virtual_size = virt_size;
                m_SectionHeader[i].m_virtual_addr = virt_addr;
            }

            // ******************************************************************
            // * generate .cxbximp section raw size / addr
            // ******************************************************************
            {
                uint32 raw_size = RoundUp(m_SectionHeader[i].m_virtual_size, PE_FILE_ALIGN);

                m_SectionHeader[i].m_sizeof_raw  = raw_size;
                m_SectionHeader[i].m_raw_addr    = dwSectionCursor;

                dwSectionCursor += raw_size;
            }

            // ******************************************************************
            // * relocation / line numbers will not exist
            // ******************************************************************
            {
                m_SectionHeader[i].m_relocations_addr = 0;
                m_SectionHeader[i].m_linenumbers_addr = 0;

                m_SectionHeader[i].m_relocations = 0;
                m_SectionHeader[i].m_linenumbers = 0;
            }

            // ******************************************************************
            // * make this section readable initialized data
            // ******************************************************************
            m_SectionHeader[i].m_characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;

            // ******************************************************************
            // * update import table directory entry
            // ******************************************************************
            m_OptionalHeader.m_image_data_directory[1].m_virtual_addr = m_SectionHeader[i].m_virtual_addr + 0x08;
            m_OptionalHeader.m_image_data_directory[1].m_size = 0x28;

            // ******************************************************************
            // * update import address table directory entry
            // ******************************************************************
            m_OptionalHeader.m_image_data_directory[12].m_virtual_addr = m_SectionHeader[i].m_virtual_addr;
            m_OptionalHeader.m_image_data_directory[12].m_size = 0x08;
        }

        // ******************************************************************
        // * generate .cxbxplg section header
        // ******************************************************************
        {
            uint32 i = m_Header.m_sections - 1;

            memcpy(m_SectionHeader[i].m_name, ".cxbxplg", 8);

            // ******************************************************************
            // * generate .cxbxplg section virtual size / addr
            // ******************************************************************
            {
                uint32 virt_size = RoundUp(0x1000 + x_Xbe->m_Header.dwSizeofHeaders, 0x1000);
                uint32 virt_addr = RoundUp(m_SectionHeader[i-1].m_virtual_addr + m_SectionHeader[i-1].m_virtual_size, PE_SEGM_ALIGN);

                m_SectionHeader[i].m_virtual_size = virt_size;
                m_SectionHeader[i].m_virtual_addr = virt_addr;

                // our entry point should be the first bytes in this section
                m_OptionalHeader.m_entry = virt_addr;
            }

            // ******************************************************************
            // * generate .cxbxplg section raw size / addr
            // ******************************************************************
            {
                uint32 raw_size = RoundUp(m_SectionHeader[i].m_virtual_size, PE_FILE_ALIGN);

                m_SectionHeader[i].m_sizeof_raw  = raw_size;
                m_SectionHeader[i].m_raw_addr    = dwSectionCursor;

                dwSectionCursor += raw_size;
            }

            // ******************************************************************
            // * relocation / line numbers will not exist
            // ******************************************************************
            {
                m_SectionHeader[i].m_relocations_addr = 0;
                m_SectionHeader[i].m_linenumbers_addr = 0;

                m_SectionHeader[i].m_relocations = 0;
                m_SectionHeader[i].m_linenumbers = 0;
            }

            // ******************************************************************
            // * make this section readable and executable
            // ******************************************************************
            m_SectionHeader[i].m_characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
        }
    }

    // ******************************************************************
    // * generate sections
    // ******************************************************************
    {
        m_bzSection = new uint08*[m_Header.m_sections];

        // ******************************************************************
        // * generate xbe sections
        // ******************************************************************
        {
            uint32 kt = x_Xbe->m_Header.dwKernelImageThunkAddr;

            // ******************************************************************
            // * decode kernel thunk address
            // ******************************************************************
            {
                if((kt ^ XOR_KT_DEBUG) > 0x01000000)
                    kt ^= XOR_KT_RETAIL;
                else
                    kt ^= XOR_KT_DEBUG;
            }

            // ******************************************************************
            // * generate xbe sections
            // ******************************************************************
            for(uint32 v=0;v<x_Xbe->m_Header.dwSections;v++)
            {
                uint32 SectionSize = m_SectionHeader[v].m_sizeof_raw;

                m_bzSection[v] = new uint08[SectionSize];

                memset(m_bzSection[v], 0, SectionSize);

                memcpy(m_bzSection[v], x_Xbe->m_bzSection[v], x_Xbe->m_SectionHeader[v].dwSizeofRaw);
            }

        }

        // ******************************************************************
        // * generate .cxbximp section
        // ******************************************************************
        {
            // ******************************************************************
            // * TODO: fix up this entire chunk of code, it is a total hack
            // ******************************************************************
            uint32 i = m_Header.m_sections - 2;

            uint32 dwVirtAddr = m_SectionHeader[i].m_virtual_addr;

            uint32 dwRawSize = m_SectionHeader[i].m_sizeof_raw;

            m_bzSection[i] = new uint08[dwRawSize];

            memset(m_bzSection[i], 0, dwRawSize);

            *(uint32*)&m_bzSection[i][0x00] = dwVirtAddr + 0x38;
            *(uint32*)&m_bzSection[i][0x04] = 0;
            *(uint32*)&m_bzSection[i][0x08] = dwVirtAddr + 0x30;
            *(uint32*)&m_bzSection[i][0x0C] = 0;

            *(uint32*)&m_bzSection[i][0x10] = 0;
            *(uint32*)&m_bzSection[i][0x14] = dwVirtAddr + 0x48;
            *(uint32*)&m_bzSection[i][0x18] = dwVirtAddr + 0x00;
            *(uint32*)&m_bzSection[i][0x1C] = 0;

            *(uint32*)&m_bzSection[i][0x20] = 0;
            *(uint32*)&m_bzSection[i][0x24] = 0;
            *(uint32*)&m_bzSection[i][0x28] = 0;
            *(uint32*)&m_bzSection[i][0x2C] = 0;

            *(uint32*)&m_bzSection[i][0x30] = dwVirtAddr + 0x38;
            *(uint32*)&m_bzSection[i][0x34] = 0;
            *(uint16*)&m_bzSection[i][0x38] = 0x0001;

            memcpy(&m_bzSection[i][0x3A], "_EmuXDummy@0\0\0cxbx.dll\0\0\0\0\0\0", 28);
        }

        // ******************************************************************
        // * generate .cxbxplg section
        // ******************************************************************
        {
            uint32 ep = x_Xbe->m_Header.dwEntryAddr;
            uint32 i  = m_Header.m_sections - 1;

            // ******************************************************************
            // * decode entry point
            // ******************************************************************
            if( (ep ^ XOR_EP_RETAIL) > 0x01000000)
                ep ^= XOR_EP_DEBUG;
            else
                ep ^= XOR_EP_RETAIL;

            m_bzSection[i] = new uint08[m_SectionHeader[i].m_sizeof_raw];

            // ******************************************************************
            // * append prolog section
            // ******************************************************************
            memcpy(m_bzSection[i], Prolog, 0x1000);

            // ******************************************************************
            // * append xbe header
            // ******************************************************************
            memcpy(m_bzSection[i] + 0x100, &x_Xbe->m_Header, sizeof(Xbe::Header));

            // ******************************************************************
            // * append xbe extra header bytes
            // ******************************************************************
            memcpy(m_bzSection[i] + 0x100 + sizeof(Xbe::Header), x_Xbe->m_HeaderEx, x_Xbe->m_Header.dwSizeofHeaders - sizeof(Xbe::Header));

            // ******************************************************************
            // * patch prolog function parameters
            // ******************************************************************
            *(uint32 *)((uint32)m_bzSection[i] + 1)  = (uint32)EmuXInit;
            *(uint32 *)((uint32)m_bzSection[i] + 6)  = (uint32)ep;
            *(uint32 *)((uint32)m_bzSection[i] + 11) = (uint32)x_Xbe->m_Header.dwSizeofHeaders;
            *(uint32 *)((uint32)m_bzSection[i] + 16) = m_SectionHeader[i].m_virtual_addr + m_OptionalHeader.m_image_base + 0x100;
            *(uint32 *)((uint32)m_bzSection[i] + 21) = x_debug_console;
        }
    }

    // ******************************************************************
    // * patch kernel thunk table
    // ******************************************************************
    {
        uint32 kt = x_Xbe->m_Header.dwKernelImageThunkAddr;

        // ******************************************************************
        // * decode kernel thunk address
        // ******************************************************************
        {
            if( (kt ^ XOR_KT_DEBUG) > 0x01000000)
                kt ^= XOR_KT_RETAIL;
            else
                kt ^= XOR_KT_DEBUG;
        }

        // ******************************************************************
        // * locate section containing kernel thunk table
        // ******************************************************************
        for(uint32 v=0;v<x_Xbe->m_Header.dwSections;v++)
        {
            uint32 imag_base = m_OptionalHeader.m_image_base;                

            uint32 virt_addr = m_SectionHeader[v].m_virtual_addr;
            uint32 virt_size = m_SectionHeader[v].m_virtual_size;

            // ******************************************************************
            // * modify kernel thunk table, if found
            // ******************************************************************
            if(kt >= virt_addr + imag_base && kt < virt_addr + virt_size + imag_base)
            {
                uint32 *kt_tbl = (uint32*)&m_bzSection[v][kt - virt_addr - imag_base];

                while(*kt_tbl != 0)
                    *kt_tbl++ = KernelThunkTable[*kt_tbl & 0x7FFFFFFF];

                break;
            }
        }
    }

    // ******************************************************************
    // * update imcomplete header fields
    // ******************************************************************
    {
        // ******************************************************************
        // * calculate size of code / data / image
        // ******************************************************************
        {
            uint32 sizeof_code   = 0;
            uint32 sizeof_data   = 0;
            uint32 sizeof_undata = 0;
            uint32 sizeof_image  = 0;

            for(uint32 v=0;v<m_Header.m_sections;v++)
            {
                uint32 characteristics = m_SectionHeader[v].m_characteristics;

                if( (characteristics & IMAGE_SCN_MEM_EXECUTE) || (characteristics & IMAGE_SCN_CNT_CODE) )
                    sizeof_code += m_SectionHeader[v].m_sizeof_raw;

                if( (characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) )
                    sizeof_data += m_SectionHeader[v].m_sizeof_raw;
            }

            // ******************************************************************
            // * calculate size of image
            // ******************************************************************
            sizeof_image  = sizeof_undata + sizeof_data + sizeof_code + m_OptionalHeader.m_sizeof_headers;
            sizeof_image  = RoundUp(sizeof_image, PE_SEGM_ALIGN);

            // ******************************************************************
            // * update optional header as necessary
            // ******************************************************************
            m_OptionalHeader.m_sizeof_code                 = sizeof_code;
            m_OptionalHeader.m_sizeof_initialized_data     = sizeof_data;
            m_OptionalHeader.m_sizeof_uninitialized_data   = sizeof_undata;
            m_OptionalHeader.m_sizeof_image                = sizeof_image;
        }

        // ******************************************************************
        // * we'll set code base as the virtual address of the first section
        // ******************************************************************
        m_OptionalHeader.m_code_base = m_SectionHeader[0].m_virtual_addr;

        // ******************************************************************
        // * we'll set data base as the virtual address of the first section
        // * that is not marked as containing code or being executable
        // ******************************************************************
        for(uint32 v=0;v<m_Header.m_sections;v++)
        {
            uint32 characteristics = m_SectionHeader[v].m_characteristics;

            if( !(characteristics & IMAGE_SCN_MEM_EXECUTE) || !(characteristics & IMAGE_SCN_CNT_CODE) )
            {
                m_OptionalHeader.m_data_base = m_SectionHeader[v].m_virtual_addr;
                break;
            }
        }
    }
}
