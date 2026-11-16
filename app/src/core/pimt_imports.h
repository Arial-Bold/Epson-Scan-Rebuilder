/* generated from imports.txt (PT) */
enum {
    I_Unwind_Resume,
    I_GetTime,
    I_sprintf,
    I_memcpy,
    I_memmove,
    I_cxa_allocate_exception,
    I_strcpy,
    I_fopen,
    I_fread,
    I_cxa_throw,
    I_fclose,
    I_fseek,
    I_Delay,
    I_cxa_begin_catch,
    I_getc,
    I_FSpDelete,
    I_memset,
    I_fwrite,
    I_cxa_end_catch,
    I_putc,
    I_FSMakeFSSpec,
    I_FSpCreate,
    I_FSRefMakePath,
    I_CFStringGetSystemEncoding,
    I_strcat,
    I_CFStringGetCString,
    I_p2cstrcpy,
    I_CFStringCreateWithPascalString,
    I_FSpMakeFSRef,
    I_CFRelease,
    I_NewPtr,
    I_DisposePtr,
    I_PBSetFPosSync,
    I_DirCreate,
    I_PBHOpenDFSync,
    I_PBUnmountVol,
    I_GetFPos,
    I_PBHRstFLockSync,
    I_FindFolder,
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
    I_SetFPos,
    I_PBHSetFLockSync,
    I_FSWrite,
    I_HDelete,
    I_TickCount,
    I_HSetVol,
    I_FreeMem,
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
    I_CompactMem,
    I_FSClose,
    I_FSGetCatalogInfo,
    I_FSPathMakeRef,
    I_FSRead,
    I_FSpGetFInfo,
    I_FSpOpenDF,
    I_Gestalt,
    I_GetEOF,
    I_exit,
    I_ferror,
    I_fflush,
    I_fprintf,
    I_free,
    I_getenv,
    I_longjmp,
    I_malloc,
    I_modf,
    I_setjmp,
    I_sscanf,
    I_strncpy,
    I_tmpfile,
    I_UNKNOWN_
};
static int imp_id(uint32_t a)
{
    switch (a) {
    case 0x11000u: return I_Unwind_Resume;
    case 0x11005u: return I_GetTime;
    case 0x1100Au: return I_sprintf;
    case 0x1100Fu: return I_memcpy;
    case 0x11014u: return I_memmove;
    case 0x11019u: return I_cxa_allocate_exception;
    case 0x1101Eu: return I_strcpy;
    case 0x11023u: return I_fopen;
    case 0x11028u: return I_fread;
    case 0x1102Du: return I_cxa_throw;
    case 0x11032u: return I_fclose;
    case 0x11037u: return I_fseek;
    case 0x1103Cu: return I_Delay;
    case 0x11041u: return I_cxa_begin_catch;
    case 0x11046u: return I_getc;
    case 0x1104Bu: return I_FSpDelete;
    case 0x11050u: return I_memset;
    case 0x11055u: return I_fwrite;
    case 0x1105Au: return I_cxa_end_catch;
    case 0x1105Fu: return I_putc;
    case 0x11064u: return I_FSMakeFSSpec;
    case 0x11069u: return I_FSpCreate;
    case 0x1106Eu: return I_FSRefMakePath;
    case 0x11073u: return I_CFStringGetSystemEncoding;
    case 0x11078u: return I_strcat;
    case 0x1107Du: return I_CFStringGetCString;
    case 0x11082u: return I_p2cstrcpy;
    case 0x11087u: return I_CFStringCreateWithPascalString;
    case 0x1108Cu: return I_FSpMakeFSRef;
    case 0x11091u: return I_CFRelease;
    case 0x11096u: return I_NewPtr;
    case 0x1109Bu: return I_DisposePtr;
    case 0x110A0u: return I_PBSetFPosSync;
    case 0x110A5u: return I_DirCreate;
    case 0x110AAu: return I_PBHOpenDFSync;
    case 0x110AFu: return I_PBUnmountVol;
    case 0x110B4u: return I_GetFPos;
    case 0x110B9u: return I_PBHRstFLockSync;
    case 0x110BEu: return I_FindFolder;
    case 0x110C3u: return I_PBGetCatInfoSync;
    case 0x110C8u: return I_NewPtrClear;
    case 0x110CDu: return I_HRename;
    case 0x110D2u: return I_PBGetUGEntrySync;
    case 0x110D7u: return I_HRstFLock;
    case 0x110DCu: return I_PBGetFCBInfoSync;
    case 0x110E1u: return I_HGetVol;
    case 0x110E6u: return I_PBReadSync;
    case 0x110EBu: return I_NumToString;
    case 0x110F0u: return I_PBGetEOFSync;
    case 0x110F5u: return I_PBSetEOFSync;
    case 0x110FAu: return I_PBHOpenRFDenySync;
    case 0x110FFu: return I_PBWriteSync;
    case 0x11104u: return I_SetFPos;
    case 0x11109u: return I_PBHSetFLockSync;
    case 0x1110Eu: return I_FSWrite;
    case 0x11113u: return I_HDelete;
    case 0x11118u: return I_TickCount;
    case 0x1111Du: return I_HSetVol;
    case 0x11122u: return I_FreeMem;
    case 0x11127u: return I_MaxBlock;
    case 0x1112Cu: return I_PBHGetFInfoSync;
    case 0x11131u: return I_CatMove;
    case 0x11136u: return I_CharacterByteType;
    case 0x1113Bu: return I_PBHOpenDenySync;
    case 0x11140u: return I_PBXGetVolInfoSync;
    case 0x11145u: return I_PBHOpenRFSync;
    case 0x1114Au: return I_PBSetCatInfoSync;
    case 0x1114Fu: return I_PBHDeleteSync;
    case 0x11154u: return I_PBHOpenSync;
    case 0x11159u: return I_PBHGetVInfoSync;
    case 0x1115Eu: return I_GetDateTime;
    case 0x11163u: return I_PBDeleteFileIDRefSync;
    case 0x11168u: return I_PBFlushFileSync;
    case 0x1116Du: return I_PBLockRangeSync;
    case 0x11172u: return I_PBHGetLogInInfoSync;
    case 0x11177u: return I_PBHCopyFileSync;
    case 0x1117Cu: return I_PBHGetVolParmsSync;
    case 0x11181u: return I_PBHMapNameSync;
    case 0x11186u: return I_PBGetVolMountInfo;
    case 0x1118Bu: return I_PBResolveFileIDRefSync;
    case 0x11190u: return I_PBExchangeFilesSync;
    case 0x11195u: return I_PBUnlockRangeSync;
    case 0x1119Au: return I_PBHCreateSync;
    case 0x1119Fu: return I_PBVolumeMount;
    case 0x111A4u: return I_PBCreateFileIDRefSync;
    case 0x111A9u: return I_PBHMapIDSync;
    case 0x111AEu: return I_PBHSetDirAccessSync;
    case 0x111B3u: return I_PBSetForeignPrivsSync;
    case 0x111B8u: return I_PBHMoveRenameSync;
    case 0x111BDu: return I_PBUnshareSync;
    case 0x111C2u: return I_PBGetForeignPrivsSync;
    case 0x111C7u: return I_PBGetVolMountInfoSize;
    case 0x111CCu: return I_PBHGetDirAccessSync;
    case 0x111D1u: return I_PBShareSync;
    default: return I_UNKNOWN_;
    }
}
/* data imports: address in the jump table, own definition (0 = scratch) */
static const uint32_t kDataImp[][2] = {
    { 0x111D6u, 0x0u },   /* ___gxx_personality_v0 */
    { 0x111DAu, 0x0u },   /* __ZTIs */
    { 0x111DEu, 0x0u },   /* __ZTIi */
};
