// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/objexcept.h"

#include "liblitesdk/litesdk_dma.h"

#ifdef _DEBUG
#include <stdio.h>
#endif

#ifdef CSR_DMA_WRITER_BASE
typedef struct _litex_dmawriter_type_t {
    mp_obj_base_t base;
    litedma_writer_t *dma;
    uint8_t num;
} litex_dmawriter_obj_t;

const mp_obj_type_t litex_dmawriter_type;

STATIC mp_obj_t litex_dmawriter_make_new(const mp_obj_type_t *type_in,
		size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    mp_uint_t dmawriter_num = mp_obj_get_int(args[0]);
    litedma_writer_t *dma = litedma_writer_instance(dmawriter_num);
    if (dma == NULL) {
         nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
			"DMAWriter number %d is not valid", dmawriter_num));
    }

    litedma_writer_init(dma);
    litex_dmawriter_obj_t *self = m_new_obj(litex_dmawriter_obj_t);
    self->dma = dma;
    self->base.type = &litex_dmawriter_type;
    self->num = dmawriter_num;

    return MP_OBJ_FROM_PTR(self);
}

void litex_dmawriter_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    litex_dmawriter_obj_t *self = (litex_dmawriter_obj_t *) self_in;
    mp_printf(print, "DMAWriter(%u): base=0x%p, len=%u, offset=%u, done=%d, started=%d",
        self->num,
        litedma_writer_get_baseaddress(self->dma),
        litedma_writer_get_length(self->dma),
        litedma_writer_get_offset(self->dma), 
        litedma_writer_is_done(self->dma),
        litedma_writer_is_started(self->dma));
}

STATIC mp_obj_t litex_dmawriter_membuf(mp_obj_t self_in, mp_obj_t mem_buf)
{
    mp_buffer_info_t dest;
    mp_get_buffer_raise(mem_buf, &dest, MP_BUFFER_WRITE);

    litedma_writer_set_buffer(((litex_dmawriter_obj_t *) self_in)->dma, dest.buf, dest.len);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(litex_dmawriter_membuf_obj, litex_dmawriter_membuf);

STATIC mp_obj_t litex_dmawriter_start(mp_obj_t self_in)
{
    litedma_writer_start(((litex_dmawriter_obj_t *) self_in)->dma);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmawriter_start_obj, litex_dmawriter_start);

STATIC mp_obj_t litex_dmawriter_stop(mp_obj_t self_in)
{
    litedma_writer_stop(((litex_dmawriter_obj_t *) self_in)->dma);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmawriter_stop_obj, litex_dmawriter_stop);

STATIC mp_obj_t litex_dmawriter_offset(mp_obj_t self_in)
{
    return MP_OBJ_NEW_SMALL_INT(litedma_writer_get_offset(((litex_dmawriter_obj_t *) self_in)->dma));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmawriter_offset_obj, litex_dmawriter_offset);

STATIC mp_obj_t litex_dmawriter_done(mp_obj_t self_in)
{
    return mp_obj_new_bool(litedma_writer_is_done(((litex_dmawriter_obj_t *) self_in)->dma));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmawriter_done_obj, litex_dmawriter_done);

STATIC mp_obj_t litex_dmawriter_invalidate(mp_obj_t self_in)
{
    //FIXME: should only invalidate [base, offset) range
    (void ) self_in;
    // Flush caches
    flush_cpu_dcache();
    flush_l2_cache();
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmawriter_invalidate_obj, litex_dmawriter_invalidate);

//STATIC mp_int_t dmawriter_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags); //TODO

STATIC const mp_map_elem_t litex_dmawriter_locals_dict_table[] = {
	{ MP_OBJ_NEW_QSTR(MP_QSTR_membuf), (mp_obj_t) &litex_dmawriter_membuf_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_start), (mp_obj_t) &litex_dmawriter_start_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_stop), (mp_obj_t) &litex_dmawriter_stop_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_offset), (mp_obj_t) &litex_dmawriter_offset_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_done), (mp_obj_t) &litex_dmawriter_done_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_invalidate), (mp_obj_t) &litex_dmawriter_invalidate_obj },
};

STATIC MP_DEFINE_CONST_DICT(litex_dmawriter_locals_dict, litex_dmawriter_locals_dict_table);

const mp_obj_type_t litex_dmawriter_type = {
	{ &mp_type_type },
	.name = MP_QSTR_DMAWriter,
	.print = litex_dmawriter_print,
	.make_new = litex_dmawriter_make_new,
        //.buffer_p = { .get_buffer = dmawriter_get_buffer }, //TODO: expose buffer
	.locals_dict = (mp_obj_t)&litex_dmawriter_locals_dict,
};

#endif //CSR_DMA_WRITER_BASE

#ifdef CSR_DMA_READER_BASE
typedef struct _litex_dmareader_type_t {
    mp_obj_base_t base;
    litedma_reader_t *dma;
    uint8_t num;
} litex_dmareader_obj_t;

const mp_obj_type_t litex_dmareader_type;

STATIC mp_obj_t litex_dmareader_make_new(const mp_obj_type_t *type_in,
		size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    mp_uint_t dmareader_num = mp_obj_get_int(args[0]);
    litedma_reader_t *dma = litedma_reader_instance(dmareader_num);
    if (dma == NULL) {
         nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
			"DMAReader number %d is not valid", dmareader_num));
    }

    litedma_reader_init(dma);
    litex_dmareader_obj_t *self = m_new_obj(litex_dmareader_obj_t);
    self->dma = dma;
    self->base.type = &litex_dmareader_type;
    self->num = dmareader_num;

    return MP_OBJ_FROM_PTR(self);
}

void litex_dmareader_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    litex_dmareader_obj_t *self = (litex_dmareader_obj_t *) self_in;
    mp_printf(print, "DMAReader(%u): base=0x%p, len=%u, offset=%u, done=%d, started=%d",
        self->num,
        litedma_reader_get_baseaddress(self->dma),
        litedma_reader_get_length(self->dma),
        litedma_reader_get_offset(self->dma), 
        litedma_reader_is_done(self->dma),
        litedma_reader_is_started(self->dma));
}

STATIC mp_obj_t litex_dmareader_membuf(mp_obj_t self_in, mp_obj_t mem_buf)
{
    mp_buffer_info_t dest;
    mp_get_buffer_raise(mem_buf, &dest, MP_BUFFER_READ);

    litedma_reader_set_buffer(((litex_dmareader_obj_t *) self_in)->dma, dest.buf, dest.len);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(litex_dmareader_membuf_obj, litex_dmareader_membuf);

STATIC mp_obj_t litex_dmareader_start(mp_obj_t self_in)
{
    litedma_reader_start(((litex_dmareader_obj_t *) self_in)->dma);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmareader_start_obj, litex_dmareader_start);

STATIC mp_obj_t litex_dmareader_stop(mp_obj_t self_in)
{
    litedma_reader_stop(((litex_dmareader_obj_t *) self_in)->dma);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmareader_stop_obj, litex_dmareader_stop);

STATIC mp_obj_t litex_dmareader_offset(mp_obj_t self_in)
{
    return MP_OBJ_NEW_SMALL_INT(litedma_reader_get_offset(((litex_dmareader_obj_t *) self_in)->dma));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmareader_offset_obj, litex_dmareader_offset);

STATIC mp_obj_t litex_dmareader_done(mp_obj_t self_in)
{
    return mp_obj_new_bool(litedma_reader_is_done(((litex_dmareader_obj_t *) self_in)->dma));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmareader_done_obj, litex_dmareader_done);

STATIC mp_obj_t litex_dmareader_flush(mp_obj_t self_in)
{
    //FIXME: should only flush [base, offset) range
    (void ) self_in;
    // Flush caches
    flush_cpu_dcache();
    flush_l2_cache();
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_dmareader_flush_obj, litex_dmareader_flush);

//STATIC mp_int_t dmareader_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags); //TODO


STATIC const mp_map_elem_t litex_dmareader_locals_dict_table[] = {
	{ MP_OBJ_NEW_QSTR(MP_QSTR_membuf), (mp_obj_t) &litex_dmareader_membuf_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_start), (mp_obj_t) &litex_dmareader_start_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_stop), (mp_obj_t) &litex_dmareader_stop_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_offset), (mp_obj_t) &litex_dmareader_offset_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_done), (mp_obj_t) &litex_dmareader_done_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_flush), (mp_obj_t) &litex_dmareader_flush_obj },
};

STATIC MP_DEFINE_CONST_DICT(litex_dmareader_locals_dict, litex_dmareader_locals_dict_table);

const mp_obj_type_t litex_dmareader_type = {
	{ &mp_type_type },
	.name = MP_QSTR_DMAReader,
	.print = litex_dmareader_print,
	.make_new = litex_dmareader_make_new,
        //.buffer_p = { .get_buffer = dmawriter_get_buffer }, //TODO: expose buffer
	.locals_dict = (mp_obj_t)&litex_dmareader_locals_dict,
};

#endif //CSR_DMA_READER_BASE

