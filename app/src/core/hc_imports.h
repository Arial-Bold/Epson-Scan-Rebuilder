/* generated from imports.txt (HC) */
enum {
    I_dlopen,
    I_keymgr_get_and_lock_processwide_ptr,
    I_free,
    I_malloc,
    I_keymgr_set_and_unlock_processwide_ptr,
    I_dlsym,
    I_keymgr_get_and_lock_processwide_ptr_2,
    I_calloc,
    I_memcpy,
    I_ZdlPv,
    I_Unwind_Resume,
    I_memset,
    I_longjmp,
    I_cos,
    I_Znwm,
    I_setjmp,
    I_floor,
    I_pow,
    I_strcpy,
    I_sprintf,
    I_bsearch,
    I_fprintf,
    I_qsort,
    I_strcmp,
    I_fwrite,
    I_memmove,
    I_log,
    I_exp,
    I_rand,
    I_atan2,
    I_realloc,
    I_open,
    I_read,
    I_fstat,
    I_write,
    I_close,
    I_lseek,
    I_vfprintf,
    I_maskrune,
    I_fputs,
    I_strchr,
    I_fputc,
    I_clearerr,
    I_fopen,
    I_strcat,
    I_fread,
    I_error,
    I_fdopen,
    I_ftell,
    I_strerror,
    I_fflush,
    I_vsnprintf,
    I_ferror,
    I_fclose,
    I_fseek,
    I_cxa_get_exception_ptr,
    I_cxa_allocate_exception,
    I_atoi,
    I_cxa_throw,
    I_cxa_begin_catch,
    I_cxa_end_catch,
    I_syslog,
    I_CFStringAppend,
    I_CFStringAppendCString,
    I_CFURLCreateWithFileSystemPathRelativeToBase,
    I_CFStringCreateMutable,
    I_CFBundleCreate,
    I_CFBundleGetFunctionPointerForName,
    I_CFRelease,
    I_ZNK5Anoop5CCubeIhE12SixTetLookupEPKhPh,
    I_ZN5Anoop5CCubeIhE5SetUpEi,
    I_ZdaPv,
    I_puts,
    I_Znam,
    I_printf,
    I_expf,
    I_abort,
    I_UNKNOWN_
};
static int imp_id(uint32_t a)
{
    switch (a) {
    case 0x9D000u: return I_dlopen;
    case 0x9D005u: return I_keymgr_get_and_lock_processwide_ptr;
    case 0x9D00Au: return I_free;
    case 0x9D00Fu: return I_malloc;
    case 0x9D014u: return I_keymgr_set_and_unlock_processwide_ptr;
    case 0x9D019u: return I_dlsym;
    case 0x9D01Eu: return I_keymgr_get_and_lock_processwide_ptr_2;
    case 0x9D023u: return I_calloc;
    case 0x9D028u: return I_memcpy;
    case 0x9D02Du: return I_ZdlPv;
    case 0x9D032u: return I_Unwind_Resume;
    case 0x9D037u: return I_memset;
    case 0x9D03Cu: return I_longjmp;
    case 0x9D041u: return I_cos;
    case 0x9D046u: return I_Znwm;
    case 0x9D04Bu: return I_setjmp;
    case 0x9D050u: return I_floor;
    case 0x9D055u: return I_pow;
    case 0x9D05Au: return I_strcpy;
    case 0x9D05Fu: return I_sprintf;
    case 0x9D064u: return I_bsearch;
    case 0x9D069u: return I_fprintf;
    case 0x9D06Eu: return I_qsort;
    case 0x9D073u: return I_strcmp;
    case 0x9D078u: return I_fwrite;
    case 0x9D07Du: return I_memmove;
    case 0x9D082u: return I_log;
    case 0x9D087u: return I_exp;
    case 0x9D08Cu: return I_rand;
    case 0x9D091u: return I_atan2;
    case 0x9D096u: return I_realloc;
    case 0x9D09Bu: return I_open;
    case 0x9D0A0u: return I_read;
    case 0x9D0A5u: return I_fstat;
    case 0x9D0AAu: return I_write;
    case 0x9D0AFu: return I_close;
    case 0x9D0B4u: return I_lseek;
    case 0x9D0B9u: return I_vfprintf;
    case 0x9D0BEu: return I_maskrune;
    case 0x9D0C3u: return I_fputs;
    case 0x9D0C8u: return I_strchr;
    case 0x9D0CDu: return I_fputc;
    case 0x9D0D2u: return I_clearerr;
    case 0x9D0D7u: return I_fopen;
    case 0x9D0DCu: return I_strcat;
    case 0x9D0E1u: return I_fread;
    case 0x9D0E6u: return I_error;
    case 0x9D0EBu: return I_fdopen;
    case 0x9D0F0u: return I_ftell;
    case 0x9D0F5u: return I_strerror;
    case 0x9D0FAu: return I_fflush;
    case 0x9D0FFu: return I_vsnprintf;
    case 0x9D104u: return I_ferror;
    case 0x9D109u: return I_fclose;
    case 0x9D10Eu: return I_fseek;
    case 0x9D113u: return I_cxa_get_exception_ptr;
    case 0x9D118u: return I_cxa_allocate_exception;
    case 0x9D11Du: return I_atoi;
    case 0x9D122u: return I_cxa_throw;
    case 0x9D127u: return I_cxa_begin_catch;
    case 0x9D12Cu: return I_cxa_end_catch;
    case 0x9D131u: return I_syslog;
    case 0x9D136u: return I_CFStringAppend;
    case 0x9D13Bu: return I_CFStringAppendCString;
    case 0x9D140u: return I_CFURLCreateWithFileSystemPathRelativeToBase;
    case 0x9D145u: return I_CFStringCreateMutable;
    case 0x9D14Au: return I_CFBundleCreate;
    case 0x9D14Fu: return I_CFBundleGetFunctionPointerForName;
    case 0x9D154u: return I_CFRelease;
    case 0x9D159u: return I_ZNK5Anoop5CCubeIhE12SixTetLookupEPKhPh;
    case 0x9D15Eu: return I_ZN5Anoop5CCubeIhE5SetUpEi;
    case 0x9D163u: return I_ZdaPv;
    case 0x9D168u: return I_puts;
    case 0x9D16Du: return I_Znam;
    case 0x9D172u: return I_printf;
    case 0x9D177u: return I_expf;
    case 0x9D17Cu: return I_abort;
    default: return I_UNKNOWN_;
    }
}
/* data imports: address in the jump table, own definition (0 = scratch) */
static const uint32_t kDataImp[][2] = {
    { 0x9D181u, 0x0u },   /* ? */
    { 0x9D185u, 0x5AACu },   /* __Z21DctAndQtiz_Chen_QuickPlPsS0_16JPEG_DC_ACCURACY */
    { 0x9D189u, 0x565Cu },   /* __Z15DctAndQtiz_ChenPlPsS0_16JPEG_DC_ACCURACY */
    { 0x9D18Du, 0x0u },   /* ___gxx_personality_v0 */
    { 0x9D191u, 0x850C0u },   /* __TIFFBuiltinCODECS */
    { 0x9D195u, 0x2AA2Cu },   /* __TIFFDefaultTileSize */
    { 0x9D199u, 0x29D78u },   /* __TIFFDefaultStripSize */
    { 0x9D19Du, 0x29930u },   /* __TIFFSwab24BitData */
    { 0x9D1A1u, 0x298D1u },   /* __TIFFNoPostDecode */
    { 0x9D1A5u, 0x29993u },   /* __TIFFSwab32BitData */
    { 0x9D1A9u, 0x298D6u },   /* __TIFFSwab16BitData */
    { 0x9D1ADu, 0x299EFu },   /* __TIFFSwab64BitData */
    { 0x9D1B1u, 0x85738u },   /* __TIFFerrorHandler */
    { 0x9D1B5u, 0x744C0u },   /* _TIFFFaxWhiteTable */
    { 0x9D1B9u, 0x7C4C0u },   /* _TIFFFaxMainTable */
    { 0x9D1BDu, 0x644C0u },   /* _TIFFFaxBlackTable */
    { 0x9D1C1u, 0x0u },   /* ___sF */
    { 0x9D1C5u, 0x0u },   /* __DefaultRuneLocale */
    { 0x9D1C9u, 0x8573Cu },   /* __TIFFwarningHandler */
    { 0x9D1CDu, 0x3616Bu },   /* _zcfree */
    { 0x9D1D1u, 0x7FF00u },   /* __length_code */
    { 0x9D1D5u, 0x88600u },   /* _z_errmsg */
    { 0x9D1D9u, 0x36158u },   /* _zcalloc */
    { 0x9D1DDu, 0x80000u },   /* __dist_code */
    { 0x9D1E1u, 0x366C4u },   /* _demo_tiff_conv */
    { 0x9D1E5u, 0x36394u },   /* _demo_jpeg_conv */
    { 0x9D1E9u, 0x3623Cu },   /* _demo_jpeg_start */
    { 0x9D1EDu, 0x36780u },   /* _demo_tiff_end */
    { 0x9D1F1u, 0x3619Au },   /* _demo_free */
    { 0x9D1F5u, 0x36186u },   /* _demo_realloc */
    { 0x9D1F9u, 0x367C0u },   /* _demo_flate_start */
    { 0x9D1FDu, 0x3649Cu },   /* _demo_tiff_start */
    { 0x9D201u, 0x368C6u },   /* _demo_flate_end */
    { 0x9D205u, 0x36832u },   /* _demo_flate_conv */
    { 0x9D209u, 0x363F0u },   /* _demo_jpeg_end */
    { 0x9D20Du, 0x3620Eu },   /* _demo_output */
    { 0x9D211u, 0x361CCu },   /* _demo_input */
    { 0x9D215u, 0x3617Cu },   /* _demo_alloc */
    { 0x9D219u, 0x361A4u },   /* _demo_process */
    { 0x9D21Du, 0x36190u },   /* _demo_avail */
    { 0x9D221u, 0x88780u },   /* __ZTV17CPDFObjectSection */
    { 0x9D225u, 0x867C0u },   /* __ZTVN3PDF4pairIiiEE */
    { 0x9D229u, 0x86A40u },   /* __ZTV17CPDFDataComponentI8CPDFFontE */
    { 0x9D22Du, 0x86C60u },   /* __ZTI18PDFOutputException */
    { 0x9D231u, 0x86840u },   /* __ZTV11CPDFSectionI14CPDFDictionaryE */
    { 0x9D235u, 0x86C40u },   /* __ZTI17PDFInputException */
    { 0x9D239u, 0x86780u },   /* __ZTI18PDFMemoryException */
    { 0x9D23Du, 0x869E0u },   /* __ZTV15CPDFFontPackage */
    { 0x9D241u, 0x86B20u },   /* __ZTVN3PDF4pairIPKcNS0_IPKhhEEEE */
    { 0x9D245u, 0x887E0u },   /* __ZTV8CPDFPage */
    { 0x9D249u, 0x86A60u },   /* __ZTVN3PDF4pairIPKcS2_EE */
    { 0x9D24Du, 0x86880u },   /* __ZTI22PDFOutOfRangeException */
    { 0x9D251u, 0x86860u },   /* __ZTI21PDFTerminateException */
    { 0x9D255u, 0x86CA0u },   /* __ZTV17EPHighCompression */
    { 0x9D259u, 0x86AE0u },   /* __ZTVN3PDF4pairIPKhhEE */
    { 0x9D25Du, 0x868E0u },   /* __ZTV10CPDFObjectIdE */
    { 0x9D261u, 0x86D20u },   /* __ZTVN3PDF4pairIPKc9CPDFArrayEE */
    { 0x9D265u, 0x86940u },   /* __ZTV14CPDFObjectBase */
    { 0x9D269u, 0x86CE0u },   /* __ZTVN3PDF4pairIPKc14CPDFDictionaryEE */
    { 0x9D26Du, 0x869C0u },   /* __ZTV17CPDFDataComponentI11CPDFXObjectE */
    { 0x9D271u, 0x86BE0u },   /* __ZTV14CPDFStreamImpl */
    { 0x9D275u, 0x887C0u },   /* __ZTV8CPDFFont */
    { 0x9D279u, 0x887A0u },   /* __ZTV11CPDFXObject */
    { 0x9D27Du, 0x86B60u },   /* __ZTV14CPDFMemoryImpl */
    { 0x9D281u, 0x86960u },   /* __ZTV15CPDFXObjPackage */
    { 0x9D285u, 0x86BC0u },   /* __ZTV10CPDFMemory */
    { 0x9D289u, 0x86800u },   /* __ZTVN3PDF4pairIPKcNS0_IiiEEEE */
    { 0x9D28Du, 0x86AA0u },   /* __ZTVN3PDF4pairIPKcdEE */
    { 0x9D291u, 0x86C80u },   /* __ZTV10CPDFStream */
    { 0x9D295u, 0x868A0u },   /* __ZTV11CPDFSectionI10CPDFObjectIdEE */
    { 0x9D299u, 0x86DC0u },   /* __ZTV10CPDFObjectIiE */
    { 0x9D29Du, 0x9C2DCu },   /* _g_encrypt */
    { 0x9D2A1u, 0x86E00u },   /* __ZTV10CPDFObjectIN3PDF4RectEE */
    { 0x9D2A5u, 0x86EC0u },   /* __ZTV10CPDFObjectIN3PDF4pairIPKhhEEE */
    { 0x9D2A9u, 0x9C2E4u },   /* _g_stream */
    { 0x9D2ADu, 0x86E40u },   /* __ZTV10CPDFObjectIN3PDF4pairIiiEEE */
    { 0x9D2B1u, 0x9C2E8u },   /* _g_mem */
    { 0x9D2B5u, 0x86E80u },   /* __ZTV10CPDFObjectIPKcE */
    { 0x9D2B9u, 0x86444u },   /* _PDFProcessProc */
    { 0x9D2BDu, 0x86F40u },   /* __ZTV10CPDFFilter */
    { 0x9D2C1u, 0x86D60u },   /* __ZTI18PDFFilterException */
    { 0x9D2C5u, 0x9C2E0u },   /* _g_filter */
    { 0x9D2C9u, 0x86FE0u },   /* __ZTVN3PDF4pairIPKciEE */
    { 0x9D2CDu, 0x0u },   /* _kCFAllocatorDefault */
    { 0x9D2D1u, 0x87100u },   /* __ZTV14CPDFEncryption */
    { 0x9D2D5u, 0x87120u },   /* __ZTVN5Anoop5CCubeIhEE */
};
