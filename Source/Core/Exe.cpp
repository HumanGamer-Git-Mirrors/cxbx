// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Core->Exe.cpp
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

#include <memory.h>
#include <stdio.h>

// ******************************************************************
// * constructor
// ******************************************************************
Exe::Exe(const char *x_szFilename)
{
    ConstructorInit();

    FILE *ExeFile = fopen(x_szFilename, "rb");

    // ******************************************************************
    // * verify exe file was opened
    // ******************************************************************
    if(ExeFile == 0)
    {
        SetError("could not open .exe file.", true);
        return;
    }

    // ******************************************************************
    // * ignore dos stub (if it exists)
    // ******************************************************************
    {
        if(fread(&m_DOSHeader.m_magic, sizeof(m_DOSHeader.m_magic), 1, ExeFile) != 1)
        {
            SetError("unexpected read error while reading magic number", true);
            goto cleanup;
        }

        if(m_DOSHeader.m_magic == *(uint16*)"MZ")
        {
            if(fread(&m_DOSHeader.m_cblp, sizeof(m_DOSHeader)-2, 1, ExeFile) != 1)
            {
                SetError("unexpected read error while reading dos header", true);
                goto cleanup;
            }

	    fseek(ExeFile, m_DOSHeader.m_lfanew, SEEK_SET);
        }
    }

    // ******************************************************************
    // * read pe header
    // ******************************************************************
    {
        if(fread(&m_Header, sizeof(m_Header), 1, ExeFile) != 1)
        {
            SetError("unexpected read error while reading pe header", true);
            goto cleanup;
        }

        if(m_Header.m_magic != *(uint32*)"PE\0\0")
        {
            SetError("invalid file,  could not locate PE header", true);
            goto cleanup;
        }
    }

    // ******************************************************************
    // * read optional header
    // ******************************************************************
    {
        if(fread(&m_OptionalHeader, sizeof(m_OptionalHeader), 1, ExeFile) != 1)
        {
            SetError("unexpected read error while reading optional header", true);
            goto cleanup;
        }

         if(m_OptionalHeader.m_magic != 0x010B)
        {
            SetError("invalid file, could not locate optional header", true);
            goto cleanup;
        }
    }

    // ******************************************************************
    // * read section headers
    // ******************************************************************
    {
        m_SectionHeader = new SectionHeader[m_Header.m_sections];

        for(uint32 v=0;v<m_Header.m_sections;v++)
        {
            if(fread(&m_SectionHeader[v], sizeof(SectionHeader), 1, ExeFile) != 1)
            {
                char buffer[255];
                sprintf(buffer, "could not read pe section header %d (%Xh)", v, v);
                SetError(buffer, true);
                goto cleanup;
            }
        }
    }

    // ******************************************************************
    // * read sections
    // ******************************************************************
    {
        m_bzSection = new uint08*[m_Header.m_sections];

        for(uint32 v=0;v<m_Header.m_sections;v++)
        {
            uint32 raw_size = m_SectionHeader[v].m_sizeof_raw;
            uint32 raw_addr = m_SectionHeader[v].m_raw_addr;

            m_bzSection[v] = new uint08[raw_size];

            memset(m_bzSection[v], 0, raw_size);

            if(raw_size == 0)
                continue;

            // ******************************************************************
            // * read current section from file (if raw_size > 0)
            // ******************************************************************
            {
	        fseek(ExeFile, raw_addr, SEEK_SET);

                if(fread(m_bzSection[v], raw_size, 1, ExeFile) != 1)
                {
                    char buffer[255];
                    sprintf(buffer, "could not read pe section %d (%Xh)", v, v);
                    SetError(buffer, true);
                    goto cleanup;
                }
            }
        }
    }

cleanup:

    fclose(ExeFile);
}

// ******************************************************************
// * ConstructorInit
// ******************************************************************
void Exe::ConstructorInit()
{
    m_SectionHeader = 0;
    m_bzSection     = 0;
}

// ******************************************************************
// * deconstructor
// ******************************************************************
Exe::~Exe()
{
    if(m_bzSection != 0)
    {
        for(uint32 v=0;v<m_Header.m_sections;v++)
            delete[] m_bzSection[v];

        delete[] m_bzSection;
    }

    delete[] m_SectionHeader;
}

// ******************************************************************
// * Export
// ******************************************************************
void Exe::Export(const char *x_szExeFilename)
{
    if(GetError() != 0)
        return;

    FILE *ExeFile = fopen(x_szExeFilename, "wb");

    // ******************************************************************
    // * verify that file was opened
    // ******************************************************************
    if(ExeFile == 0)
    {
        SetError("could not open .exe file.", false);
        return;
    }

    // ******************************************************************
    // * write dos stub
    // ******************************************************************
    {
        if(fwrite(bzDOSStub, sizeof(bzDOSStub), 1, ExeFile) != 1)
        {
            SetError("could not write dos stub", false);
            goto cleanup;
        }
    }

    // ******************************************************************
    // * write pe header
    // ******************************************************************
    {
        if(fwrite(&m_Header, sizeof(Header), 1, ExeFile) != 1)
        {
            SetError("could not write pe header", false);
            goto cleanup;
        }
    }

    // ******************************************************************
    // * write optional header
    // ******************************************************************
    {
        if(fwrite(&m_OptionalHeader, sizeof(OptionalHeader), 1, ExeFile) != 1)
        {
            SetError("could not write pe optional header", false);
            goto cleanup;
        }
    }

    // ******************************************************************
    // * write section headers
    // ******************************************************************
    {
        for(uint32 v=0;v<m_Header.m_sections;v++)
        {
            if(fwrite(&m_SectionHeader[v], sizeof(SectionHeader), 1, ExeFile) != 1)
            {
                char buffer[255];
                sprintf(buffer, "could not write pe section header %d (%Xh)", v, v);
                SetError(buffer, false);
                goto cleanup;
            }
        }
    }

    // ******************************************************************
    // * write sections
    // ******************************************************************
    {
        for(uint32 v=0;v<m_Header.m_sections;v++)
        {
            uint32 RawSize = m_SectionHeader[v].m_sizeof_raw;
            uint32 RawAddr = m_SectionHeader[v].m_raw_addr;

	    fseek(ExeFile, RawAddr, SEEK_SET);

            if(RawSize == 0)
                continue;

            if(fwrite(m_bzSection[v], RawSize, 1, ExeFile) != 1)
            {
                char Buffer[255];
                sprintf(Buffer, "could not write pe section %d (%Xh)", v, v);
                SetError(Buffer, false);
                goto cleanup;
            }
        }
    }

cleanup:

    fclose(ExeFile);

    return;
}

// ******************************************************************
// * GetAddr
// ******************************************************************
uint08 *Exe::GetAddr(uint32 x_dwVirtualAddress)
{
    for(uint32 v=0;v<m_Header.m_sections;v++)
    {
        uint32 virt_addr = m_SectionHeader[v].m_virtual_addr;
        uint32 virt_size = m_SectionHeader[v].m_virtual_size;

        if( (x_dwVirtualAddress >= virt_addr) && (x_dwVirtualAddress < (virt_addr + virt_size)) )
            return &m_bzSection[v][x_dwVirtualAddress - virt_addr];
    }

    return 0;
}
