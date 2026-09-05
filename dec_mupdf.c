/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / document decoder filter, based on MuPDF
 *  (https://mupdf.com/) - a standalone renderer covering XPS/OpenXPS, CBZ
 *  comic archives, EPUB, FictionBook and Mobipocket.
 *
 *  A document is a page sequence, not an image: the first page is rendered to
 *  RGB, the same convention the PDF filter (poppler) already follows in this
 *  repo. PDF is deliberately left out of the capabilities so the two filters do
 *  not compete for the same files.
 *
 *  MuPDF picks its handler from a "magic" string, so the filter passes the
 *  source file extension down (same need as the RECOIL filter).
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <mupdf/fitz.h>

/* Rendering scale: XPS and EPUB pages are described in points, so a page comes
 * out at 72 dpi without it, which is unreadable for text. */
#define MUPDFDEC_ZOOM 2.0f
/* An oversized page would allocate an unreasonable pixmap. */
#define MUPDFDEC_MAX_PIXELS (64 * 1024 * 1024)

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_MUPDFDecCtx;

/* MuPDF needs a name to choose its document handler; the extension is enough. */
static void mupdfdec_magic(GF_FilterPid *pid, char *buf, u32 buf_size)
{
	const GF_PropertyValue *p;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILEPATH);
	if (!p)
		p = gf_filter_pid_get_property(pid, GF_PROP_PID_URL);
	if (p && p->value.string)
	{
		strncpy(buf, p->value.string, buf_size - 1);
		buf[buf_size - 1] = 0;
		return;
	}

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILE_EXT);
	if (p && p->value.string)
		snprintf(buf, buf_size, "document.%s", p->value.string);
	else
		snprintf(buf, buf_size, "document.xps");
}

static GF_Err mupdfdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_MUPDFDecCtx *ctx = (GF_MUPDFDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool mupdfdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_MUPDFDecCtx *ctx = (GF_MUPDFDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err mupdfdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size, y;
	char magic[GF_MAX_PATH];
	fz_context *fctx = NULL;
	fz_buffer *buf = NULL;
	fz_stream *stm = NULL;
	fz_document *doc = NULL;
	fz_page *page = NULL;
	fz_pixmap *pix = NULL;
	GF_Err e = GF_NON_COMPLIANT_BITSTREAM;
	GF_MUPDFDecCtx *ctx = (GF_MUPDFDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	mupdfdec_magic(ctx->ipid, magic, sizeof(magic));

	fctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!fctx)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	/* MuPDF reports errors by longjmp'ing out of fz_try, so everything below
	 * runs inside one. */
	fz_try(fctx)
	{
		fz_register_document_handlers(fctx);

		buf = fz_new_buffer_from_copied_data(fctx, (const unsigned char *)data, size);
		stm = fz_open_buffer(fctx, buf);
		doc = fz_open_document_with_stream(fctx, magic, stm);

		if (fz_count_pages(fctx, doc) > 0)
		{
			fz_matrix m = fz_scale(MUPDFDEC_ZOOM, MUPDFDEC_ZOOM);
			page = fz_load_page(fctx, doc, 0);
			pix = fz_new_pixmap_from_page(fctx, page, m, fz_device_rgb(fctx), 0);
		}
	}
	fz_catch(fctx)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[MuPDFDec] %s\n", fz_caught_message(fctx)));
		pix = NULL;
	}

	gf_filter_pid_drop_packet(ctx->ipid);

	if (!pix || (pix->w <= 0) || (pix->h <= 0) || (pix->n != 3))
		goto exit;
	if ((u64)pix->w * pix->h > MUPDFDEC_MAX_PIXELS)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[MuPDFDec] Page too large: %dx%d\n", pix->w, pix->h));
		goto exit;
	}

	out_size = (u32)pix->w * (u32)pix->h * 3;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT((u32)pix->w));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT((u32)pix->h));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT((u32)pix->w * 3));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		e = GF_OUT_OF_MEM;
		goto exit;
	}

	/* A pixmap row is stride bytes, which is not always w*3. */
	for (y = 0; y < (u32)pix->h; y++)
		memcpy(output + (size_t)y * pix->w * 3, pix->samples + (size_t)y * pix->stride, (size_t)pix->w * 3);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);
	e = GF_EOS;

exit:
	fz_try(fctx)
	{
		if (pix) fz_drop_pixmap(fctx, pix);
		if (page) fz_drop_page(fctx, page);
		if (doc) fz_drop_document(fctx, doc);
		if (stm) fz_drop_stream(fctx, stm);
		if (buf) fz_drop_buffer(fctx, buf);
	}
	fz_catch(fctx)
	{
	}
	fz_drop_context(fctx);

	if (e == GF_EOS)
		gf_filter_pid_set_eos(ctx->opid);
	return e;
}

static void mupdfdec_finalize(GF_Filter *filter)
{
}

/* PDF is handled by the poppler filter in this repo and is left out on
 * purpose, so the two never compete for the same file. */
static const GF_FilterCapability MUPDFDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "xps|oxps|cbz|epub|fb2|mobi"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "application/oxps|application/vnd.ms-xpsdocument|application/x-cbz|application/epub+zip"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister MUPDFDecoderRegister = {
	.name = "mupdfdec",
	GF_FS_SET_DESCRIPTION("Document decoder (XPS, CBZ, EPUB, FB2, MOBI)")
		GF_FS_SET_HELP("This filter renders the first page of an XPS, CBZ, EPUB, FictionBook or Mobipocket document to a raw image using MuPDF.")
			.private_size = sizeof(GF_MUPDFDecCtx),
	SETCAPS(MUPDFDecCaps),
	.configure_pid = mupdfdec_configure_pid,
	.process = mupdfdec_process,
	.process_event = mupdfdec_process_event,
	.finalize = mupdfdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE mupdfdec_register(GF_FilterSession *session)
{
	return &MUPDFDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_mupdfdec(void) {
    gf_filter_auto_register("mupdfdec", mupdfdec_register);
}
