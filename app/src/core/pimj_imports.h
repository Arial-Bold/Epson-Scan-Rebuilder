/* generated from imports.txt (PJ) */
enum {
    I_cxa_allocate_exception,
    I_strcpy,
    I_fopen,
    I_fread,
    I_cxa_throw,
    I_fclose,
    I_fseek,
    I_Unwind_Resume,
    I_GetTime,
    I_memcpy,
    I_sprintf,
    I_memmove,
    I_memset,
    I_FSRefMakePath,
    I_CFStringGetSystemEncoding,
    I_strcat,
    I_CFStringGetCString,
    I_GetGWorldPixMap,
    I_GetPixBaseAddr,
    I_LockPixels,
    I_FSpDelete,
    I_p2cstrcpy,
    I_UnlockPixels,
    I_CFStringCreateWithPascalString,
    I_FSMakeFSSpec,
    I_FSpMakeFSRef,
    I_GetPortBounds,
    I_CFRelease,
    I_FSWrite,
    I_SetFPos,
    I_NewPtr,
    I_FindFolder,
    I_FSpGetFInfo,
    I_DisposePtr,
    I_FSRead,
    I_FSClose,
    I_Gestalt,
    I_FSpOpenDF,
    I_FreeMem,
    I_CompactMem,
    I_strncpy,
    I_FSpCreate,
    I_GetEOF,
    I_FSPathMakeRef,
    I_FSGetCatalogInfo,
    I_putc,
    I_setjmp,
    I_fwrite,
    I_tmpfile,
    I_Delay,
    I_getc,
    I_fprintf,
    I_longjmp,
    I_modf,
    I_fflush,
    I_ferror,
    I_exit,
    I_sscanf,
    I_getenv,
    I_PBSetFPosSync,
    I_DirCreate,
    I_PBHOpenDFSync,
    I_PBUnmountVol,
    I_GetFPos,
    I_PBHRstFLockSync,
    I_PBGetCatInfoSync,
    I_NewPtrClear,
    I_HRename,
    I_PBGetUGEntrySync,
    I_HRstFLock,
    I_PBGetFCBInfoSync,
    I_HGetVol,
    I_PBReadSync,
    I_NumToString,
    I_PBGetEOFSync,
    I_PBSetEOFSync,
    I_PBHOpenRFDenySync,
    I_PBWriteSync,
    I_PBHSetFLockSync,
    I_HDelete,
    I_TickCount,
    I_HSetVol,
    I_MaxBlock,
    I_PBHGetFInfoSync,
    I_CatMove,
    I_CharacterByteType,
    I_PBHOpenDenySync,
    I_PBXGetVolInfoSync,
    I_PBHOpenRFSync,
    I_PBSetCatInfoSync,
    I_PBHDeleteSync,
    I_PBHOpenSync,
    I_PBHGetVInfoSync,
    I_GetDateTime,
    I_PBDeleteFileIDRefSync,
    I_PBFlushFileSync,
    I_PBLockRangeSync,
    I_PBHGetLogInInfoSync,
    I_PBHCopyFileSync,
    I_PBHGetVolParmsSync,
    I_PBHMapNameSync,
    I_PBGetVolMountInfo,
    I_PBResolveFileIDRefSync,
    I_PBExchangeFilesSync,
    I_PBUnlockRangeSync,
    I_PBHCreateSync,
    I_PBVolumeMount,
    I_PBCreateFileIDRefSync,
    I_PBHMapIDSync,
    I_PBHSetDirAccessSync,
    I_PBSetForeignPrivsSync,
    I_PBHMoveRenameSync,
    I_PBUnshareSync,
    I_PBGetForeignPrivsSync,
    I_PBGetVolMountInfoSize,
    I_PBHGetDirAccessSync,
    I_PBShareSync,
    I_cxa_begin_catch,
    I_cxa_end_catch,
    I_free,
    I_malloc,
    I_UNKNOWN_
};
static int imp_id(uint32_t a)
{
    switch (a) {
    case 0x3A000u: return I_cxa_allocate_exception;
    case 0x3A005u: return I_strcpy;
    case 0x3A00Au: return I_fopen;
    case 0x3A00Fu: return I_fread;
    case 0x3A014u: return I_cxa_throw;
    case 0x3A019u: return I_fclose;
    case 0x3A01Eu: return I_fseek;
    case 0x3A023u: return I_Unwind_Resume;
    case 0x3A028u: return I_GetTime;
    case 0x3A02Du: return I_memcpy;
    case 0x3A032u: return I_sprintf;
    case 0x3A037u: return I_memmove;
    case 0x3A03Cu: return I_memset;
    case 0x3A041u: return I_FSRefMakePath;
    case 0x3A046u: return I_CFStringGetSystemEncoding;
    case 0x3A04Bu: return I_strcat;
    case 0x3A050u: return I_CFStringGetCString;
    case 0x3A055u: return I_GetGWorldPixMap;
    case 0x3A05Au: return I_GetPixBaseAddr;
    case 0x3A05Fu: return I_LockPixels;
    case 0x3A064u: return I_FSpDelete;
    case 0x3A069u: return I_p2cstrcpy;
    case 0x3A06Eu: return I_UnlockPixels;
    case 0x3A073u: return I_CFStringCreateWithPascalString;
    case 0x3A078u: return I_FSMakeFSSpec;
    case 0x3A07Du: return I_FSpMakeFSRef;
    case 0x3A082u: return I_GetPortBounds;
    case 0x3A087u: return I_CFRelease;
    case 0x3A08Cu: return I_FSWrite;
    case 0x3A091u: return I_SetFPos;
    case 0x3A096u: return I_NewPtr;
    case 0x3A09Bu: return I_FindFolder;
    case 0x3A0A0u: return I_FSpGetFInfo;
    case 0x3A0A5u: return I_DisposePtr;
    case 0x3A0AAu: return I_FSRead;
    case 0x3A0AFu: return I_FSClose;
    case 0x3A0B4u: return I_Gestalt;
    case 0x3A0B9u: return I_FSpOpenDF;
    case 0x3A0BEu: return I_FreeMem;
    case 0x3A0C3u: return I_CompactMem;
    case 0x3A0C8u: return I_strncpy;
    case 0x3A0CDu: return I_FSpCreate;
    case 0x3A0D2u: return I_GetEOF;
    case 0x3A0D7u: return I_FSPathMakeRef;
    case 0x3A0DCu: return I_FSGetCatalogInfo;
    case 0x3A0E1u: return I_putc;
    case 0x3A0E6u: return I_setjmp;
    case 0x3A0EBu: return I_fwrite;
    case 0x3A0F0u: return I_tmpfile;
    case 0x3A0F5u: return I_Delay;
    case 0x3A0FAu: return I_getc;
    case 0x3A0FFu: return I_fprintf;
    case 0x3A104u: return I_longjmp;
    case 0x3A109u: return I_modf;
    case 0x3A10Eu: return I_fflush;
    case 0x3A113u: return I_ferror;
    case 0x3A118u: return I_exit;
    case 0x3A11Du: return I_sscanf;
    case 0x3A122u: return I_getenv;
    case 0x3A127u: return I_PBSetFPosSync;
    case 0x3A12Cu: return I_DirCreate;
    case 0x3A131u: return I_PBHOpenDFSync;
    case 0x3A136u: return I_PBUnmountVol;
    case 0x3A13Bu: return I_GetFPos;
    case 0x3A140u: return I_PBHRstFLockSync;
    case 0x3A145u: return I_PBGetCatInfoSync;
    case 0x3A14Au: return I_NewPtrClear;
    case 0x3A14Fu: return I_HRename;
    case 0x3A154u: return I_PBGetUGEntrySync;
    case 0x3A159u: return I_HRstFLock;
    case 0x3A15Eu: return I_PBGetFCBInfoSync;
    case 0x3A163u: return I_HGetVol;
    case 0x3A168u: return I_PBReadSync;
    case 0x3A16Du: return I_NumToString;
    case 0x3A172u: return I_PBGetEOFSync;
    case 0x3A177u: return I_PBSetEOFSync;
    case 0x3A17Cu: return I_PBHOpenRFDenySync;
    case 0x3A181u: return I_PBWriteSync;
    case 0x3A186u: return I_PBHSetFLockSync;
    case 0x3A18Bu: return I_HDelete;
    case 0x3A190u: return I_TickCount;
    case 0x3A195u: return I_HSetVol;
    case 0x3A19Au: return I_MaxBlock;
    case 0x3A19Fu: return I_PBHGetFInfoSync;
    case 0x3A1A4u: return I_CatMove;
    case 0x3A1A9u: return I_CharacterByteType;
    case 0x3A1AEu: return I_PBHOpenDenySync;
    case 0x3A1B3u: return I_PBXGetVolInfoSync;
    case 0x3A1B8u: return I_PBHOpenRFSync;
    case 0x3A1BDu: return I_PBSetCatInfoSync;
    case 0x3A1C2u: return I_PBHDeleteSync;
    case 0x3A1C7u: return I_PBHOpenSync;
    case 0x3A1CCu: return I_PBHGetVInfoSync;
    case 0x3A1D1u: return I_GetDateTime;
    case 0x3A1D6u: return I_PBDeleteFileIDRefSync;
    case 0x3A1DBu: return I_PBFlushFileSync;
    case 0x3A1E0u: return I_PBLockRangeSync;
    case 0x3A1E5u: return I_PBHGetLogInInfoSync;
    case 0x3A1EAu: return I_PBHCopyFileSync;
    case 0x3A1EFu: return I_PBHGetVolParmsSync;
    case 0x3A1F4u: return I_PBHMapNameSync;
    case 0x3A1F9u: return I_PBGetVolMountInfo;
    case 0x3A1FEu: return I_PBResolveFileIDRefSync;
    case 0x3A203u: return I_PBExchangeFilesSync;
    case 0x3A208u: return I_PBUnlockRangeSync;
    case 0x3A20Du: return I_PBHCreateSync;
    case 0x3A212u: return I_PBVolumeMount;
    case 0x3A217u: return I_PBCreateFileIDRefSync;
    case 0x3A21Cu: return I_PBHMapIDSync;
    case 0x3A221u: return I_PBHSetDirAccessSync;
    case 0x3A226u: return I_PBSetForeignPrivsSync;
    case 0x3A22Bu: return I_PBHMoveRenameSync;
    case 0x3A230u: return I_PBUnshareSync;
    case 0x3A235u: return I_PBGetForeignPrivsSync;
    case 0x3A23Au: return I_PBGetVolMountInfoSize;
    case 0x3A23Fu: return I_PBHGetDirAccessSync;
    case 0x3A244u: return I_PBShareSync;
    default: return I_UNKNOWN_;
    }
}
/* data imports: address in the jump table, own definition (0 = scratch) */
static const uint32_t kDataImp[][2] = {
    { 0x3A249u, 0x0u },   /* __ZTIs */
    { 0x3A24Du, 0x0u },   /* ___gxx_personality_v0 */
    { 0x3A251u, 0x39004u },   /* _g_bSetJpegWork */
    { 0x3A255u, 0x0u },   /* ___sF */
    { 0x3A259u, 0x393C0u },   /* _g_JpegWorkSpec */
    { 0x3A25Du, 0x2676Eu },   /* __Z15jpeg_fdct_ifastPi */
    { 0x3A261u, 0x26C16u },   /* __Z15jpeg_fdct_islowPi */
    { 0x3A265u, 0x2619Eu },   /* __Z15jpeg_fdct_floatPf */
    { 0x3A269u, 0x38E20u },   /* _jpeg_natural_order */
    { 0x3A26Du, 0x21AEEu },   /* _jpeg_resync_to_restart */
    { 0x3A271u, 0x27B4Eu },   /* __Z15jpeg_idct_ifastP22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A275u, 0x29748u },   /* __Z13jpeg_idct_1x1P22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A279u, 0x28D30u },   /* __Z13jpeg_idct_4x4P22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A27Du, 0x29390u },   /* __Z13jpeg_idct_2x2P22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A281u, 0x27342u },   /* __Z15jpeg_idct_floatP22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A285u, 0x28302u },   /* __Z15jpeg_idct_islowP22jpeg_decompress_structP19jpeg_component_infoPsPPhj */
    { 0x3A289u, 0x0u },   /* __ZTIi */
};
