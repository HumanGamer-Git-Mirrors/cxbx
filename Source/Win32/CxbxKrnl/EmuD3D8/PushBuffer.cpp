// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuD3D->PushBuffer.cpp
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
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_DEFEXTRN_

#include "Emu.h"
#include "EmuXTL.h"
#include "ResourceTracker.h"

bool XTL::g_bStepPush = false;
bool XTL::g_bSkipPush = false;
bool XTL::g_bBrkPush  = false;

bool g_bPBSkipPusher = false;

static void DbgDumpMesh(WORD *pIndexData, DWORD dwCount);

// pushbuffer execution emulation
void XTL::EmuExecutePushBuffer
(
    X_D3DPushBuffer       *pPushBuffer,
    X_D3DFixup            *pFixup
)
{
    if(g_bSkipPush)
        return;

    if(pFixup != NULL)
        EmuCleanup("PushBuffer has fixups\n");

    DWORD *pdwPushData = (DWORD*)pPushBuffer->Data;
    PVOID pIndexData = 0;

    // cache of last 4 indices
    WORD pIBMem[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

    D3DPRIMITIVETYPE    PCPrimitiveType = (D3DPRIMITIVETYPE)-1;
    X_D3DPRIMITIVETYPE  XBPrimitiveType = X_D3DPT_INVALID;

    // TODO: This technically should be enabled
    XTL::EmuUpdateDeferredStates();

    #ifdef _DEBUG_TRACK_PB
    bool bShowPB = false;

    g_PBTrackTotal.insert(pdwPushData);

    if(g_PBTrackShowOnce.exists(pdwPushData))
    {
        g_PBTrackShowOnce.remove(pdwPushData);

        printf("\n");
        printf("\n");
        printf("  PushBuffer@0x%.08X...\n", pdwPushData);
        printf("\n");

        bShowPB = true;
    }
    #endif

    static LPDIRECT3DINDEXBUFFER8 pIndexBuffer=0;
    static uint maxIBSize = 0;

    while(true)
    {
        DWORD dwCount = (*pdwPushData >> 18);
        DWORD dwMethod = (*pdwPushData & 0x3FFFF);

        // Interpret GPU Instruction
        if(dwMethod == 0x000017FC) // NVPB_SetBeginEnd
        {
            pdwPushData++;

            #ifdef _DEBUG_TRACK_PB
            if(bShowPB)
            {
                printf("  NVPB_SetBeginEnd(");
            }
            #endif

            if(*pdwPushData == 0)
            {
                #ifdef _DEBUG_TRACK_PB
                if(bShowPB)
                {
                    printf("DONE)\n");
                }
                #endif
                break;  // done?
            }
            else
            {
                #ifdef _DEBUG_TRACK_PB
                if(bShowPB)
                {
                    printf("PrimitiveType := %d)\n", *pdwPushData);
                }
                #endif

                XBPrimitiveType = (X_D3DPRIMITIVETYPE)*pdwPushData;
                PCPrimitiveType = EmuPrimitiveType(XBPrimitiveType);
            }
        }
        else if(dwMethod == 0x1808) // NVPB_FixLoop
        {
            #ifdef _DEBUG_TRACK_PB
            if(bShowPB)
            {
                printf("  NVPB_FixLoop(%d)\n", dwCount);
                printf("\n");
                printf("  Index Array Data...\n");

                WORD *pwVal = (WORD*)(pdwPushData + 1);

                for(uint s=0;s<dwCount;s++)
                {
                    if(s%8 == 0) printf("\n  ");

                    printf("  %.04X", *pwVal++);
                }

                printf("\n");
                printf("\n");
            }
            #endif

            WORD *pwVal = (WORD*)(pdwPushData + 1);
            for(uint mi=0;mi<dwCount;mi++)
            {
                pIBMem[mi+2] = pwVal[mi];
            }

            // perform rendering
            if(pIBMem[0] != 0xFFFF)
            {
                HRESULT hRet;

                // TODO: depreciate maxIBSize after N milliseconds..then N milliseconds later drop down to new highest
                if((dwCount*2 + 2*2) > maxIBSize)
                {
                    if(pIndexBuffer != 0)
                    {
                        pIndexBuffer->Release();
                    }

                    hRet = g_pD3DDevice8->CreateIndexBuffer(dwCount*2 + 2*2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIndexBuffer);

                    maxIBSize = dwCount*2 + 2*2;
                }
                else
                {
                    hRet = D3D_OK;
                }

                if(FAILED(hRet))
                    EmuCleanup("Unable to create index buffer for PushBuffer emulation (0x1808, dwCount : %d)", dwCount);

                // copy index data
                {
                    WORD *pData=0;

                    pIndexBuffer->Lock(0, dwCount*2 + 2*2, (UCHAR**)&pData, NULL);

                    memcpy(pData, pIBMem, dwCount*2 + 2*2);

                    pIndexBuffer->Unlock();
                }

                // render indexed vertices
                {
                    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(XBPrimitiveType, dwCount + 2);
                    VertexPatchDesc VPDesc;

                    VPDesc.dwVertexCount = dwCount;
                    VPDesc.PrimitiveType = XBPrimitiveType;
                    VPDesc.dwPrimitiveCount = PrimitiveCount;
                    VPDesc.dwOffset = 0;
                    VPDesc.pVertexStreamZeroData = 0;
                    VPDesc.uiVertexStreamZeroStride = 0;
                    // TODO: Set the current shader and let the patcher handle it..
                    VPDesc.hVertexShader = g_CurrentVertexShader;

                    VertexPatcher VertPatch;

                    bool bPatched = VertPatch.Apply(&VPDesc);

                    g_pD3DDevice8->SetIndices(pIndexBuffer, 0);

                    #ifdef _DEBUG_TRACK_PB
                    if(!g_PBTrackDisable.exists((PVOID)pPushBuffer->Data))
                    {
                    #endif

                    if(!g_bPBSkipPusher)
                    {
                        if(IsValidCurrentShader())
                        {
                            g_pD3DDevice8->DrawIndexedPrimitive
                            (
                                PCPrimitiveType, 0, 8*1024*1024, 0, PrimitiveCount
//                                PCPrimitiveType, 0, dwCount*2, 0, PrimitiveCount
                            );
                        }
                    }

                    #ifdef _DEBUG_TRACK_PB
                    }
                    #endif

                    VertPatch.Restore();

                    g_pD3DDevice8->SetIndices(0, 0);
                }
            }

            pdwPushData += dwCount;
        }
        else if(dwMethod == 0x1800) // NVPB_InlineArray
        {
            BOOL bInc = *pdwPushData & 0x40000000;

            if(bInc)
            {
                dwCount = ((*pdwPushData - (0x40000000 | 0x00001818)) >> 18)*2 + 2;
            }

            pIndexData = ++pdwPushData;

            #ifdef _DEBUG_TRACK_PB
            if(bShowPB)
            {
                printf("  NVPB_InlineArray(0x%.08X, %d)...\n", pIndexData, dwCount);
                printf("\n");
                printf("  Index Array Data...\n");

                WORD *pwVal = (WORD*)pIndexData;

                for(uint s=0;s<dwCount;s++)
                {
                    if(s%8 == 0) printf("\n  ");

                    printf("  %.04X", *pwVal++);
                }

                printf("\n");

                XTL::IDirect3DVertexBuffer8 *pActiveVB = NULL;

                D3DVERTEXBUFFER_DESC VBDesc;

                BYTE *pVBData = 0;
                UINT  uiStride;

                // retrieve stream data
                g_pD3DDevice8->GetStreamSource(0, &pActiveVB, &uiStride);

                // retrieve stream desc
                pActiveVB->GetDesc(&VBDesc);

                // unlock just in case
                pActiveVB->Unlock();

                // grab ptr
                pActiveVB->Lock(0, 0, &pVBData, D3DLOCK_READONLY);

                // print out stream data
                {
                    printf("\n");
                    printf("  Vertex Stream Data (0x%.08X)...\n", pActiveVB);
                    printf("\n");
                    printf("  Format : %d\n", VBDesc.Format);
                    printf("  Size   : %d bytes\n", VBDesc.Size);
                    printf("  FVF    : 0x%.08X\n", VBDesc.FVF);
                    printf("\n");
                }

                // release ptr
                pActiveVB->Unlock();

                DbgDumpMesh(pIndexData, dwCount);
            }
            #endif
          
            pdwPushData += (dwCount/2) - (bInc ? 0 : 2);
            
            // perform rendering
            {
                HRESULT hRet;

                // TODO: depreciate maxIBSize after N milliseconds..then N milliseconds later drop down to new highest
                if(dwCount*2 > maxIBSize)
                {
                    if(pIndexBuffer != 0)
                    {
                        pIndexBuffer->Release();
                    }

                    hRet = g_pD3DDevice8->CreateIndexBuffer(dwCount*2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIndexBuffer);

                    maxIBSize = dwCount*2;
                }
                else
                {
                    hRet = D3D_OK;
                }

                if(FAILED(hRet))
                    EmuCleanup("Unable to create index buffer for PushBuffer emulation (0x1800, dwCount : %d)", dwCount);

                // copy index data
                {
                    WORD *pData=0;

                    pIndexBuffer->Lock(0, dwCount*2, (UCHAR**)&pData, NULL);

                    memcpy(pData, pIndexData, dwCount*2);

                    // remember last 2 indices
                    if(dwCount >= 2)
                    {
                        pIBMem[0] = pData[dwCount - 2];
                        pIBMem[1] = pData[dwCount - 1];
                    }
                    else
                    {
                        pIBMem[0] = 0xFFFF;
                    }

                    pIndexBuffer->Unlock();
                }

                // render indexed vertices
                {
                    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(XBPrimitiveType, dwCount);
                    VertexPatchDesc VPDesc;

                    VPDesc.dwVertexCount = dwCount;
                    VPDesc.PrimitiveType = XBPrimitiveType;
                    VPDesc.dwPrimitiveCount = PrimitiveCount;
                    VPDesc.dwOffset = 0;
                    VPDesc.pVertexStreamZeroData = 0;
                    VPDesc.uiVertexStreamZeroStride = 0;
                    // TODO: Set the current shader and let the patcher handle it..
                    VPDesc.hVertexShader = g_CurrentVertexShader;

                    VertexPatcher VertPatch;

                    bool bPatched = VertPatch.Apply(&VPDesc);

                    g_pD3DDevice8->SetIndices(pIndexBuffer, 0);

                    #ifdef _DEBUG_TRACK_PB
                    if(!g_PBTrackDisable.exists((PVOID)pPushBuffer->Data))
                    {
                    #endif

                    if(!g_bPBSkipPusher)
                    {
                        if(IsValidCurrentShader())
                        {
                            //g_pD3DDevice8->SetVertexShader(D3DFVF_XYZ | D3DFVF_TEX0);

                            /* these don't help the turok lighting problems :[
                            g_pD3DDevice8->SetRenderState(D3DRS_LIGHTING, FALSE);
                            g_pD3DDevice8->SetRenderState(D3DRS_FOGENABLE, FALSE);
                            g_pD3DDevice8->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
                            g_pD3DDevice8->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
                            g_pD3DDevice8->SetRenderState(D3DRS_ZVISIBLE, FALSE);
                            g_pD3DDevice8->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
                            g_pD3DDevice8->SetRenderState(D3DRS_AMBIENT, RGB(255,125,125));
                            g_pD3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);

                            D3DMATERIAL8 mtrl;

                            ZeroMemory( &mtrl, sizeof(D3DMATERIAL8) );

                            mtrl.Diffuse.r = mtrl.Ambient.r = mtrl.Specular.r = 1.0f;
                            mtrl.Diffuse.g = mtrl.Ambient.g = mtrl.Specular.r = 0.0f;
                            mtrl.Diffuse.b = mtrl.Ambient.b = mtrl.Specular.r = 0.0f;
                            mtrl.Diffuse.a = mtrl.Ambient.a = mtrl.Specular.r = 1.0f;

                            g_pD3DDevice8->SetMaterial(&mtrl);
                            //*/
                            g_pD3DDevice8->DrawIndexedPrimitive
                            (
                                PCPrimitiveType, 0, /*dwCount*2*/8*1024*1024, 0, PrimitiveCount
                            );
                        }
                    }

                    #ifdef _DEBUG_TRACK_PB
                    }
                    #endif

                    VertPatch.Restore();

                    g_pD3DDevice8->SetIndices(0, 0);
                }
            }

            pdwPushData--;
        }
        else
        {
            EmuCleanup("Unknown PushBuffer Operation (0x%.04X, %d)", dwMethod, dwCount);
        }

        pdwPushData++;
    }

    #ifdef _DEBUG_TRACK_PB
    if(bShowPB)
    {
        printf("\n");
        printf("CxbxDbg> ");
        fflush(stdout);
    }
    #endif

    if(g_bStepPush)
    {
        g_pD3DDevice8->Present(0,0,0,0);
        Sleep(500);
    }
}

#ifdef _DEBUG_TRACK_PB
void DbgDumpMesh(WORD *pIndexData, DWORD dwCount)
{
    if(!XTL::IsValidCurrentShader() || (dwCount == 0))
        return;

    XTL::IDirect3DVertexBuffer8 *pActiveVB = NULL;

    XTL::D3DVERTEXBUFFER_DESC VBDesc;

    BYTE *pVBData = 0;
    UINT  uiStride;

    // retrieve stream data
    g_pD3DDevice8->GetStreamSource(0, &pActiveVB, &uiStride);

    char szFileName[128];
    sprintf(szFileName, "C:\\TurokMesh-0x%.08X.x", pIndexData);
    FILE *dbgVertices = fopen(szFileName, "wt");

    // retrieve stream desc
    pActiveVB->GetDesc(&VBDesc);

    // unlock just in case
    pActiveVB->Unlock();

    // grab ptr
    pActiveVB->Lock(0, 0, &pVBData, D3DLOCK_READONLY);

    // print out stream data
    {
        uint32 maxIndex = 0;

        WORD *pwChk = (WORD*)pIndexData;

        for(uint chk=0;chk<dwCount;chk++)
        {
            DWORD x = *pwChk++;

            if(x > maxIndex)
                maxIndex = x;
        }

        if(maxIndex > ((VBDesc.Size/uiStride) - 1))
            maxIndex = (VBDesc.Size / uiStride) - 1;

        fprintf(dbgVertices, "xof 0303txt 0032\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "//\n");
        fprintf(dbgVertices, "//  Vertex Stream Data (0x%.08X)...\n", pActiveVB);
        fprintf(dbgVertices, "//\n");
        fprintf(dbgVertices, "//  Format : %d\n", VBDesc.Format);
        fprintf(dbgVertices, "//  Size   : %d bytes\n", VBDesc.Size);
        fprintf(dbgVertices, "//  FVF    : 0x%.08X\n", VBDesc.FVF);
        fprintf(dbgVertices, "//  iCount : %d\n", dwCount/2);
        fprintf(dbgVertices, "//\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "Frame SCENE_ROOT {\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "  FrameTransformMatrix {\n");
        fprintf(dbgVertices, "    1.000000,0.000000,0.000000,0.000000,\n");
        fprintf(dbgVertices, "    0.000000,1.000000,0.000000,0.000000,\n");
        fprintf(dbgVertices, "    0.000000,0.000000,1.000000,0.000000,\n");
        fprintf(dbgVertices, "    0.000000,0.000000,0.000000,1.000000;;\n");
        fprintf(dbgVertices, "  }\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "  Frame Turok1 {\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "    FrameTransformMatrix {\n");
        fprintf(dbgVertices, "      1.000000,0.000000,0.000000,0.000000,\n");
        fprintf(dbgVertices, "      0.000000,1.000000,0.000000,0.000000,\n");
        fprintf(dbgVertices, "      0.000000,0.000000,1.000000,0.000000,\n");
        fprintf(dbgVertices, "      0.000000,0.000000,0.000000,1.000000;;\n");
        fprintf(dbgVertices, "    }\n");
        fprintf(dbgVertices, "\n");
        fprintf(dbgVertices, "    Mesh {\n");
        fprintf(dbgVertices, "      %d;\n", maxIndex+1);

        uint max = maxIndex+1;
        for(uint v=0;v<max;v++)
        {
            fprintf(dbgVertices, "      %f;%f;%f;%s\n", 
                *(FLOAT*)&pVBData[v*uiStride+0],
                *(FLOAT*)&pVBData[v*uiStride+4],
                *(FLOAT*)&pVBData[v*uiStride+8],
                (v < (max - 1)) ? "," : ";");
        }

        fprintf(dbgVertices, "      %d;\n", dwCount - 2);

        WORD *pwVal = (WORD*)pIndexData;

        max = dwCount;

        DWORD a = *pwVal++;
        DWORD b = *pwVal++;
        DWORD c = *pwVal++;

        DWORD la = a,lb = b,lc = c;

        for(uint i=2;i<max;i++)
        {
            fprintf(dbgVertices, "      3;%d,%d,%d;%s\n",
                a,b,c, (i < (max - 1)) ? "," : ";");

            a = b;
            b = c;
            c = *pwVal++;

            la = a;
            lb = b;
            lc = c;
        }

        fprintf(dbgVertices, "    }\n");
        fprintf(dbgVertices, "  }\n");
        fprintf(dbgVertices, "}\n");

        fclose(dbgVertices);
    }

    // release ptr
    pActiveVB->Unlock();
}
#endif
