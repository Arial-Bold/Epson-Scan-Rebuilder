/* PDF output the way Epson's PDF plug-in (CPdf / CEpPDFCreate) and PDFLib build it: one image
 * per page, JPEG (DCTDecode) with PDFLib's own quantization tables (High Compression: the
 * EPHighCompression layers, background + foreground through a CCITT G4 mask), placed at its physical size
 * from the top-left margin on the chosen paper, or on a page the size of the image. */
#ifndef ES_PDF_H
#define ES_PDF_H
#include <stddef.h>

typedef struct {
    const char *raw;      /* headerless interleaved samples, 8 or 16 bit (host order) */
    int w, h, spp, bits;
    int dpi;
} EsPdfPage;

/* paper_w10 / paper_h10: paper in 1/10 mm (0 = Actual Image Size); landscape swaps them.
 * left10 / top10: margins in 1/10 mm. high: "High Compression". */
int es_save_pdf(const char *out, const EsPdfPage *pages, int count, int paper_w10, int paper_h10,
                int landscape, int left10, int top10, int high, char *err, size_t errlen);

/* PDFLib for High Compression (PDFLib_i386); without it High Compression pages are plain JPEG */
void es_pdf_set_hc_engine(const char *path);
/* CPdf::DoCheckHighCompLimit */
int es_pdf_high_fits(int w, int h, int landscape);

#endif
