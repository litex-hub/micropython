// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include <string.h> //for memcpy

#include <generated/csr.h>

#ifdef CSR_VIDEO_FRAMEBUFFER_BASE

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"


#ifndef VIDEO_FRAMEBUFFER_DEPTH
#warning VIDEO_FRAMEBUFFER_DEPTH is hardcoded! should be specified at SoC generation
#define VIDEO_FRAMEBUFFER_DEPTH 32
#endif

typedef struct _litex_video_type_t {
    mp_obj_base_t base;
    void *video_addr;
    int xres, yres;
    size_t stride;
    uint8_t bitdepth;
    uint8_t num;
} litex_video_obj_t;

const mp_obj_type_t litex_video_type;

STATIC mp_obj_t litex_video_make_new(const mp_obj_type_t *type_in,
		size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    mp_uint_t video_num = mp_obj_get_int(args[0]);

    if (video_num != 0) {
         nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
			"not a valid video number: %d", video_num));
    }
    litex_video_obj_t *self = m_new_obj(litex_video_obj_t);
    self->base.type = &litex_video_type;
    self->num = video_num;
    self->xres = video_framebuffer_hres_read(); //VIDEO_FRAMEBUFFER_HRES
    self->yres = video_framebuffer_vres_read(); //VIDEO_FRAMEBUFFER_VRES
    self->bitdepth = VIDEO_FRAMEBUFFER_DEPTH;
    self->video_addr = (void *) video_framebuffer_base_read(); //reads word at VIDEO_FRAMEBUFFER_BASE
    self->stride = self->xres*VIDEO_FRAMEBUFFER_DEPTH/8;

    memset(self->video_addr, 0/*xFF*/, self->yres*self->stride);

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_int_t video_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    (void)flags;
    litex_video_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = self->video_addr;
    bufinfo->len = self->stride * self->yres;
    bufinfo->typecode = 'B'; // view buffer as bytes
    return 0;
}

void litex_video_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;
    mp_printf(print, "video(%u): %dx%d %dbpp, stride=%d bytes, base address=0x%p",
      self->num, self->xres, self->yres, self->bitdepth, self->stride, self->video_addr);
}

STATIC mp_obj_t litex_video_width(mp_obj_t self_in)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;
    return MP_OBJ_NEW_SMALL_INT(self->xres);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_video_width_obj, litex_video_width);

STATIC mp_obj_t litex_video_height(mp_obj_t self_in)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;
    return MP_OBJ_NEW_SMALL_INT(self->yres);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_video_height_obj, litex_video_height);

STATIC mp_obj_t litex_video_stride(mp_obj_t self_in)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;
    int stride = self->stride/(self->bitdepth/8);
    return MP_OBJ_NEW_SMALL_INT(stride); //expected result is in pixels
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_video_stride_obj, litex_video_stride);

STATIC mp_obj_t litex_video_bpp(mp_obj_t self_in)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;
    return MP_OBJ_NEW_SMALL_INT(self->bitdepth);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_video_bpp_obj, litex_video_bpp);


STATIC mp_obj_t litex_video_blitbuf(mp_obj_t self_in, mp_obj_t mem_buf)
{
    litex_video_obj_t *self = (litex_video_obj_t *) self_in;

    mp_buffer_info_t src;
    mp_get_buffer_raise(mem_buf, &src, MP_BUFFER_READ);

    size_t len = self->yres*self->stride;
    if (len > src.len)
      len = src.len;
    //memcpy(self->video_addr, src.buf, len);
    uint16_t pix = *(uint16_t*) src.buf;
    for(uint16_t *p = (uint16_t*) self->video_addr; p < ((uint16_t*) self->video_addr)+(self->yres*self->stride); ++p)
     *p = pix;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(litex_video_blitbuf_obj, litex_video_blitbuf);


STATIC const mp_map_elem_t litex_video_locals_dict_table[] = {
	{ MP_OBJ_NEW_QSTR(MP_QSTR_width), (mp_obj_t) &litex_video_width_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_height), (mp_obj_t) &litex_video_height_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_bpp), (mp_obj_t) &litex_video_bpp_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_stride), (mp_obj_t) &litex_video_stride_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_blitbuf), (mp_obj_t) &litex_video_blitbuf_obj },
};

STATIC MP_DEFINE_CONST_DICT(litex_video_locals_dict, litex_video_locals_dict_table);

const mp_obj_type_t litex_video_type = {
	{ &mp_type_type },
	.name = MP_QSTR_Video,
	.print = litex_video_print,
	.make_new = litex_video_make_new,
        .buffer_p = { .get_buffer = video_get_buffer },
	.locals_dict = (mp_obj_t)&litex_video_locals_dict,
};

#endif //CSR_VIDEO_FRAMEBUFFER_BASE

