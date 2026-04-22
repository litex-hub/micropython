// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause
//
// network.LAN — MicroPython wrapper around the LiteEth netif.
//
//   import network
//   lan = network.LAN(0)
//   lan.active(True)
//   lan.ifconfig('dhcp')
//   print(lan.ifconfig())

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#if MICROPY_PY_NETWORK && defined(CSR_ETHMAC_BASE)

#include "extmod/modnetwork.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"

#include "liteeth_netif.h"

typedef struct _network_lan_obj_t {
    mp_obj_base_t base;
    struct netif netif;
    bool active;
} network_lan_obj_t;

extern const mp_obj_type_t network_lan_type;

// One LiteEth instance per SoC for now. If a future SoC ever exposes
// CSR_ETHMAC2_BASE this becomes an array indexed by id.
static network_lan_obj_t network_lan_obj = {
    .base = { &network_lan_type },
    .active = false,
};

static mp_obj_t network_lan_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    if (n_args == 1) {
        int id = mp_obj_get_int(all_args[0]);
        if (id != 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("only LAN(0) is supported"));
        }
    }
    return MP_OBJ_FROM_PTR(&network_lan_obj);
}

static void network_lan_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    network_lan_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const ip_addr_t *ip = netif_ip_addr4(&self->netif);
    mp_printf(print, "LAN(active=%d, ip=%u.%u.%u.%u)",
        self->active,
        ip4_addr1_16(ip), ip4_addr2_16(ip),
        ip4_addr3_16(ip), ip4_addr4_16(ip));
}

// LAN.active([state]) — bring the netif up or down. With no argument,
// returns the current state.
static mp_obj_t network_lan_active(size_t n_args, const mp_obj_t *args) {
    network_lan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        return mp_obj_new_bool(self->active);
    }
    bool wanted = mp_obj_is_true(args[1]);
    if (wanted && !self->active) {
        if (liteeth_netif_init(&self->netif, NULL) != 0) {
            mp_raise_OSError(MP_ENOMEM);
        }
        self->active = true;
        // Register the NIC with extmod's socket layer so socket()
        // calls find a route to it.
        mod_network_register_nic(MP_OBJ_FROM_PTR(self));
    } else if (!wanted && self->active) {
        liteeth_netif_deinit(&self->netif);
        self->active = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(network_lan_active_obj, 1, 2,
    network_lan_active);

// LAN.isconnected() — link is up AND we have an IP.
static mp_obj_t network_lan_isconnected(mp_obj_t self_in) {
    network_lan_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bool linked = self->active && netif_is_link_up(&self->netif);
    bool got_ip = !ip4_addr_isany_val(*netif_ip4_addr(&self->netif));
    return mp_obj_new_bool(linked && got_ip);
}
static MP_DEFINE_CONST_FUN_OBJ_1(network_lan_isconnected_obj,
    network_lan_isconnected);

// LAN.ifconfig([config]) — get/set IP, mask, gateway, DNS. Routes
// through the shared lwIP helper so behaviour matches stm32/mimxrt
// ('dhcp' / tuple / no-arg).
static mp_obj_t network_lan_ifconfig(size_t n_args, const mp_obj_t *args) {
    network_lan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("LAN not active"));
    }
    return mod_network_nic_ifconfig(&self->netif, n_args - 1, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(network_lan_ifconfig_obj, 1, 2,
    network_lan_ifconfig);

// LAN.ipconfig(...) — modern keyword form (v4=, dhcp4=, ...). Same
// shared helper.
static mp_obj_t network_lan_ipconfig(size_t n_args, const mp_obj_t *args,
    mp_map_t *kwargs) {
    network_lan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->active) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("LAN not active"));
    }
    return mod_network_nic_ipconfig(&self->netif, n_args - 1, args + 1, kwargs);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(network_lan_ipconfig_obj, 1,
    network_lan_ipconfig);

// LAN.config('mac' | mac=...) — read or set the hwaddr.
static mp_obj_t network_lan_config(size_t n_args, const mp_obj_t *args,
    mp_map_t *kwargs) {
    network_lan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (kwargs->used == 0) {
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("must query one param"));
        }
        switch (mp_obj_str_get_qstr(args[1])) {
            case MP_QSTR_mac:
                return mp_obj_new_bytes(self->netif.hwaddr, 6);
            default:
                mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
        }
    }
    // Setter form.
    for (size_t i = 0; i < kwargs->alloc; i++) {
        if (!MP_MAP_SLOT_IS_FILLED(kwargs, i)) {
            continue;
        }
        mp_map_elem_t *e = &kwargs->table[i];
        switch (mp_obj_str_get_qstr(e->key)) {
            case MP_QSTR_mac: {
                mp_buffer_info_t buf;
                mp_get_buffer_raise(e->value, &buf, MP_BUFFER_READ);
                if (buf.len != 6) {
                    mp_raise_ValueError(MP_ERROR_TEXT("mac must be 6 bytes"));
                }
                memcpy(self->netif.hwaddr, buf.buf, 6);
                break;
            }
            default:
                mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(network_lan_config_obj, 1,
    network_lan_config);

static const mp_rom_map_elem_t network_lan_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_active),      MP_ROM_PTR(&network_lan_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_isconnected), MP_ROM_PTR(&network_lan_isconnected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig),    MP_ROM_PTR(&network_lan_ifconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR_ipconfig),    MP_ROM_PTR(&network_lan_ipconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR_config),      MP_ROM_PTR(&network_lan_config_obj) },
};
static MP_DEFINE_CONST_DICT(network_lan_locals_dict,
    network_lan_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    network_lan_type,
    MP_QSTR_LAN,
    MP_TYPE_FLAG_NONE,
    make_new, network_lan_make_new,
    print, network_lan_print,
    locals_dict, &network_lan_locals_dict
    );

// Drive the polling RX path from the main scheduler tick. main.c's
// REPL loop calls mp_handle_pending() between bytecodes; this hook
// runs whenever that fires. Cheap when the netif isn't active.
void litex_lwip_poll(void) {
    if (network_lan_obj.active) {
        liteeth_netif_poll(&network_lan_obj.netif);
    }
    sys_check_timeouts();
}

#endif // MICROPY_PY_NETWORK && CSR_ETHMAC_BASE
