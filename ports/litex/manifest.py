# MicroPython frozen-module manifest for ports/litex.
#
# Currently empty: the port does not ship any frozen .py modules. The file
# exists so that MICROPY_FROZEN_MANIFEST can be wired up from a board or
# CI variant without changing the port itself. See
# https://docs.micropython.org/en/latest/reference/manifest.html for the
# manifest DSL.

# Example additions a downstream consumer might want:
#   freeze("modules")                 # any *.py under ports/litex/modules/
#   require("onewire")                # micropython-lib package
